/**
 * @file    test_ota_bundle.c
 * @brief   压缩包 OTA 安装器：闸门文件、巡检状态机、sidecar 对账、失败重试与残留锁
 *
 * @note    在临时目录里打真实 tar.gz，包内 upgrade.sh 记一次调用并按 ret 文件退出。
 */

#include "adapters/outbound/ota/ota_bundle.h"
#include "common/log.h"
#include "runtime/scheduler/periodic_task.h"
#include "wdf_test_spec.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_ROOT_MAX 64U
#define TEST_PATH_MAX 160U
#define TEST_CMD_MAX  768U

static char             s_root[TEST_ROOT_MAX];
static char             s_state_dir[TEST_PATH_MAX];
static char             s_work_dir[TEST_PATH_MAX];
static char             s_bundle[TEST_PATH_MAX];
static char             s_allow[TEST_PATH_MAX];
static char             s_busy[TEST_PATH_MAX];
static char             s_cmd[TEST_CMD_MAX];
static unsigned         s_sidecar_calls;
static ota_bundle_cfg_t s_cfg;

sw_err_t periodic_task_register(const char *name, uint32_t period_ms, periodic_task_fn_t fn, void *ctx,
                                int sched_policy, int prio, size_t stack_size)
{
    (void)name;
    (void)period_ms;
    (void)fn;
    (void)ctx;
    (void)sched_policy;
    (void)prio;
    (void)stack_size;
    return SW_OK;
}

void sw_log_write(sw_log_level_t level, const char *source, const char *fmt, ...)
{
    (void)level;
    (void)source;
    (void)fmt;
}

const char *sw_log_source_file_name(const char *path)
{
    return path;
}

static void sh(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void sh(const char *fmt, ...)
{
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(s_cmd, sizeof(s_cmd), fmt, ap);
    va_end(ap);
    TEST_ASSERT_TRUE((n > 0) && ((size_t)n < sizeof(s_cmd)));
    TEST_ASSERT_EQUAL_INT(0, system(s_cmd));
}

static bool exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static unsigned upgrade_calls(void)
{
    char     path[TEST_PATH_MAX];
    FILE    *fp;
    unsigned lines = 0U;
    int      ch;

    (void)snprintf(path, sizeof(path), "%s/count", s_root);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return 0U;
    }
    while ((ch = fgetc(fp)) != EOF) {
        if (ch == '\n') {
            lines++;
        }
    }
    (void)fclose(fp);
    return lines;
}

static void set_upgrade_exit(int code)
{
    sh("echo %d > '%s/ret'", code, s_root);
}

static void set_ticket(bool on)
{
    ota_bundle_installer()->set_allowed(on);
}

/**
 * @brief  打一份落盘包
 * @param  with_upgrade 含 upgrade/upgrade.sh
 * @param  with_evil    含顶层非法成员
 */
static void write_bundle(bool with_upgrade, bool with_evil)
{
    sh("rm -rf '%s/pkg' && mkdir -p '%s/pkg/upgrade'", s_root, s_root);
    sh("printf '#!/bin/sh\\necho x >> \"%s/count\"\\nexit \"$(cat \"%s/ret\")\"\\n' "
       "> '%s/pkg/upgrade/upgrade.sh'",
       s_root, s_root, s_root);
    sh("echo evil > '%s/pkg/params.json'", s_root);
    sh("tar -C '%s/pkg' -czf '%s' %s %s", s_root, s_bundle,
       with_upgrade ? "upgrade" : "", with_evil ? "params.json" : "");
}

static void write_matching_sidecar(void)
{
    sh("sha256sum '%s' > '%s.sha256'", s_bundle, s_bundle);
}

static sw_err_t stub_write_sidecar(const char *bundle_path)
{
    s_sidecar_calls++;
    TEST_ASSERT_EQUAL_STRING(s_bundle, bundle_path);
    write_matching_sidecar();
    return SW_OK;
}

static void poll_n(unsigned n)
{
    unsigned i;

    for (i = 0U; i < n; i++) {
        ota_bundle_poll();
    }
}

void setUp(void)
{
    (void)snprintf(s_root, sizeof(s_root), "/tmp/wdf-ota-bundle-XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(s_root));
    (void)snprintf(s_state_dir, sizeof(s_state_dir), "%s/state", s_root);
    (void)snprintf(s_work_dir, sizeof(s_work_dir), "%s/work", s_root);
    (void)snprintf(s_bundle, sizeof(s_bundle), "%s/work/update.tar.gz", s_root);
    (void)snprintf(s_allow, sizeof(s_allow), "%s/state/allow", s_root);
    (void)snprintf(s_busy, sizeof(s_busy), "%s/state/busy", s_root);
    TEST_ASSERT_EQUAL_INT(0, mkdir(s_work_dir, 0700));
    set_upgrade_exit(0);
    s_sidecar_calls        = 0U;
    s_cfg.state_dir        = s_state_dir;
    s_cfg.work_dir         = s_work_dir;
    s_cfg.bundle_name      = "update.tar.gz";
    s_cfg.install_unit     = "wdf-ota-test-absent.service";
    s_cfg.write_sidecar    = NULL;
    ota_bundle_reset_for_test();
    TEST_ASSERT_EQUAL_INT(SW_OK, ota_bundle_configure(&s_cfg));
}

void tearDown(void)
{
    ota_bundle_reset_for_test();
    sh("rm -rf '%s'", s_root);
}

static void test_ticket_written_and_removed(void)
{
    TEST_ASSERT_FALSE(exists(s_state_dir));
    set_ticket(true);
    TEST_ASSERT_TRUE(exists(s_allow));
    set_ticket(false);
    TEST_ASSERT_FALSE(exists(s_allow));
}

static void test_ready_bundle_applies_once(void)
{
    char applying[TEST_PATH_MAX + 16U];

    set_ticket(true);
    write_bundle(true, false);
    write_matching_sidecar();
    ota_bundle_poll();
    TEST_ASSERT_EQUAL_UINT(0U, upgrade_calls());
    ota_bundle_poll();
    TEST_ASSERT_EQUAL_UINT(1U, upgrade_calls());
    (void)snprintf(applying, sizeof(applying), "%s.applying", s_bundle);
    TEST_ASSERT_FALSE(exists(s_bundle));
    TEST_ASSERT_FALSE(exists(applying));
    ota_bundle_poll();
    TEST_ASSERT_EQUAL_UINT(1U, upgrade_calls());
}

static void test_no_ticket_waits_then_applies(void)
{
    write_bundle(true, false);
    write_matching_sidecar();
    poll_n(3U);
    TEST_ASSERT_EQUAL_UINT(0U, upgrade_calls());
    set_ticket(true);
    ota_bundle_poll();
    TEST_ASSERT_EQUAL_UINT(1U, upgrade_calls());
}

static void test_truncated_bundle_latches(void)
{
    struct stat st;

    set_ticket(true);
    write_bundle(true, false);
    TEST_ASSERT_EQUAL_INT(0, stat(s_bundle, &st));
    TEST_ASSERT_EQUAL_INT(0, truncate(s_bundle, st.st_size / 2));
    write_matching_sidecar();
    poll_n(3U);
    TEST_ASSERT_EQUAL_UINT(0U, upgrade_calls());
    TEST_ASSERT_TRUE(exists(s_bundle));
}

static void test_evil_member_latches(void)
{
    set_ticket(true);
    write_bundle(true, true);
    write_matching_sidecar();
    poll_n(3U);
    TEST_ASSERT_EQUAL_UINT(0U, upgrade_calls());
    TEST_ASSERT_TRUE(exists(s_bundle));
}

static void test_no_sidecar_waits(void)
{
    set_ticket(true);
    write_bundle(true, false);
    poll_n(3U);
    TEST_ASSERT_EQUAL_UINT(0U, upgrade_calls());
    write_matching_sidecar();
    ota_bundle_poll();
    TEST_ASSERT_EQUAL_UINT(1U, upgrade_calls());
}

static void test_sidecar_mismatch_latches_until_ticket_returns(void)
{
    set_ticket(true);
    write_bundle(true, false);
    sh("echo 'deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef  x' > '%s.sha256'",
       s_bundle);
    poll_n(3U);
    TEST_ASSERT_EQUAL_UINT(0U, upgrade_calls());
    write_matching_sidecar();
    ota_bundle_poll();
    TEST_ASSERT_EQUAL_UINT(0U, upgrade_calls());
    set_ticket(false);
    ota_bundle_poll();
    set_ticket(true);
    ota_bundle_poll();
    TEST_ASSERT_EQUAL_UINT(1U, upgrade_calls());
}

static void test_write_sidecar_hook_fills_missing(void)
{
    s_cfg.write_sidecar = stub_write_sidecar;
    set_ticket(true);
    write_bundle(true, false);
    poll_n(2U);
    TEST_ASSERT_EQUAL_UINT(1U, s_sidecar_calls);
    TEST_ASSERT_EQUAL_UINT(1U, upgrade_calls());
}

static void test_apply_failure_retries_when_ticket_returns(void)
{
    set_ticket(true);
    set_upgrade_exit(1);
    write_bundle(true, false);
    write_matching_sidecar();
    poll_n(2U);
    TEST_ASSERT_EQUAL_UINT(1U, upgrade_calls());
    TEST_ASSERT_TRUE(exists(s_bundle));
    ota_bundle_poll();
    TEST_ASSERT_EQUAL_UINT(1U, upgrade_calls());
    set_ticket(false);
    ota_bundle_poll();
    set_upgrade_exit(0);
    set_ticket(true);
    ota_bundle_poll();
    TEST_ASSERT_EQUAL_UINT(2U, upgrade_calls());
    TEST_ASSERT_FALSE(exists(s_bundle));
}

static void test_busy_lock_and_stale_cleanup(void)
{
    const ota_installer_ops_t *ops = ota_bundle_installer();

    TEST_ASSERT_FALSE(ops->is_installing());
    sh("mkdir -p '%s' && touch '%s'", s_state_dir, s_busy);
    TEST_ASSERT_TRUE(ops->is_installing());
    TEST_ASSERT_EQUAL_INT(SW_OK, ota_bundle_configure(&s_cfg));
    TEST_ASSERT_FALSE(exists(s_busy));
    TEST_ASSERT_FALSE(ops->is_installing());
}

static void test_configure_rejects_unsafe(void)
{
    ota_bundle_cfg_t bad = s_cfg;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, ota_bundle_configure(NULL));
    bad.state_dir = "relative/state";
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, ota_bundle_configure(&bad));
    bad           = s_cfg;
    bad.work_dir  = "/tmp/it's";
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, ota_bundle_configure(&bad));
    bad             = s_cfg;
    bad.bundle_name = "sub/update.tar.gz";
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, ota_bundle_configure(&bad));
    bad              = s_cfg;
    bad.install_unit = "";
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, ota_bundle_configure(&bad));
    set_ticket(true);
    TEST_ASSERT_FALSE(exists(s_allow));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_ticket_written_and_removed, "UPG-03", "安装器按门闩写入与撤销闸门文件");
    WDF_RUN_TEST(test_ready_bundle_applies_once, "UPG-05",
                 "大小稳定、成员合法、有闸门且 sidecar 对上才交接，同一份包只交接一次");
    WDF_RUN_TEST(test_no_ticket_waits_then_applies, "UPG-05", "无闸门不交接，闸门出现后交接");
    WDF_RUN_TEST(test_truncated_bundle_latches, "UPG-06", "半截包不交接并闩住");
    WDF_RUN_TEST(test_evil_member_latches, "UPG-06", "外层含 upgrade/ 以外成员不交接");
    WDF_RUN_TEST(test_no_sidecar_waits, "UPG-06", "无 sidecar 时等待，补上后交接");
    WDF_RUN_TEST(test_sidecar_mismatch_latches_until_ticket_returns, "UPG-06",
                 "摘要不符闩住，闸门再现后重新对账");
    WDF_RUN_TEST(test_write_sidecar_hook_fills_missing, "UPG-06", "缺 sidecar 时按通道任务摘要补写");
    WDF_RUN_TEST(test_apply_failure_retries_when_ticket_returns, "UPG-07",
                 "安装失败改回原名，闸门再现再试同一份包");
    WDF_RUN_TEST(test_busy_lock_and_stale_cleanup, "UPG-08",
                 "安装锁存在即安装中；安装单元不在运行时 configure 清掉残留锁");
    WDF_RUN_TEST(test_configure_rejects_unsafe, "", "非绝对路径、shell 特殊字符或含斜杠文件名拒绝配置");
    return UNITY_END();
}
