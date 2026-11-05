/**
 * @file    ota_bundle.c
 * @brief   压缩包 OTA 本机安装器实现
 *
 * @note    巡检状态与命令缓冲只在周期任务线程使用；configure 在调度启动前完成。
 *          s_installing 由巡检线程写、开洗判断线程读，用原子量。
 */

#include "adapters/outbound/ota/ota_bundle.h"

#include "common/log.h"
#include "runtime/config/thread_config.h"
#include "runtime/scheduler/periodic_task.h"

#include <errno.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/** 大小连续不变的拍数；半截 gzip 过不了 tar 列表 */
#define OTA_BUNDLE_SIZE_STABLE_POLLS 2U
/** `tar -tzf` 与摘要计算超时（秒） */
#define OTA_BUNDLE_LIST_TIMEOUT_SEC 15U
/** 解外层包超时（秒） */
#define OTA_BUNDLE_EXTRACT_TIMEOUT_SEC 60U
/** `upgrade.sh` 超时（秒）；脚本须以不阻塞方式拉起安装单元 */
#define OTA_BUNDLE_UPGRADE_SH_TIMEOUT_SEC 60U
/** 命令行缓冲：两条路径加固定文本 */
#define OTA_BUNDLE_CMD_MAX (OTA_BUNDLE_PATH_MAX * 3U)
/** tar 成员行缓冲 */
#define OTA_BUNDLE_MEMBER_MAX 256U
/** 闸门与锁目录权限 */
#define OTA_BUNDLE_STATE_DIR_MODE 0750
/** 外层包唯一允许的顶层目录 */
#define OTA_BUNDLE_UPGRADE_DIR_NAME "upgrade"
/** 外层包内的安装脚本 */
#define OTA_BUNDLE_UPGRADE_SH_NAME "upgrade.sh"

static const ota_bundle_cfg_t *s_cfg;
static atomic_bool             s_installing;
static int64_t                 s_seen_size = -1;
static uint32_t                s_stable_polls;
static bool                    s_apply_attempted;
static bool                    s_latched;
static bool                    s_logged_latch;
static char                    s_allow_path[OTA_BUNDLE_PATH_MAX];
static char                    s_busy_path[OTA_BUNDLE_PATH_MAX];
static char                    s_bundle_path[OTA_BUNDLE_PATH_MAX];
static char                    s_applying_path[OTA_BUNDLE_PATH_MAX];
static char                    s_sha256_path[OTA_BUNDLE_PATH_MAX];
static char                    s_md5_path[OTA_BUNDLE_PATH_MAX];
static char                    s_upgrade_dir[OTA_BUNDLE_PATH_MAX];
static char                    s_upgrade_sh[OTA_BUNDLE_PATH_MAX];
static char                    s_cmd[OTA_BUNDLE_CMD_MAX];

/**
 * @brief  能否安全拼进单引号或双引号 shell 参数
 */
static bool shell_text_ok(const char *s)
{
    if ((s == NULL) || (s[0] == '\0')) {
        return false;
    }
    return strpbrk(s, "'\"$`\\\r\n") == NULL;
}

static bool abs_path_ok(const char *s)
{
    return shell_text_ok(s) && (s[0] == '/');
}

static bool fill(char *buf, const char *a, const char *sep, const char *b)
{
    int n = snprintf(buf, OTA_BUNDLE_PATH_MAX, "%s%s%s", a, sep, b);

    return (n > 0) && ((size_t)n < OTA_BUNDLE_PATH_MAX);
}

static bool fill_paths(const ota_bundle_cfg_t *cfg)
{
    return fill(s_allow_path, cfg->state_dir, "/", "allow") &&
           fill(s_busy_path, cfg->state_dir, "/", "busy") &&
           fill(s_bundle_path, cfg->work_dir, "/", cfg->bundle_name) &&
           fill(s_applying_path, s_bundle_path, "", ".applying") &&
           fill(s_sha256_path, s_bundle_path, "", ".sha256") &&
           fill(s_md5_path, s_bundle_path, "", ".md5") &&
           fill(s_upgrade_dir, cfg->work_dir, "/", OTA_BUNDLE_UPGRADE_DIR_NAME) &&
           fill(s_upgrade_sh, s_upgrade_dir, "/", OTA_BUNDLE_UPGRADE_SH_NAME);
}

static void reset_poll_state(void)
{
    s_seen_size       = -1;
    s_stable_polls    = 0U;
    s_apply_attempted = false;
    s_latched         = false;
    s_logged_latch    = false;
}

static bool fmt_cmd(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static bool run_cmd(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief  拼命令到 s_cmd
 * @return false 超长
 */
static bool vfmt_cmd(const char *fmt, va_list ap)
{
    int n = vsnprintf(s_cmd, sizeof(s_cmd), fmt, ap);

    if ((n < 0) || ((size_t)n >= sizeof(s_cmd))) {
        LOG_WARN("ota_bundle: command too long");
        return false;
    }
    return true;
}

static bool fmt_cmd(const char *fmt, ...)
{
    va_list ap;
    bool    ok;

    va_start(ap, fmt);
    ok = vfmt_cmd(fmt, ap);
    va_end(ap);
    return ok;
}

/**
 * @brief  拼命令并执行，失败记日志
 * @return true 退出码为 0
 */
static bool run_cmd(const char *fmt, ...)
{
    va_list ap;
    bool    ok;
    int     rc;

    va_start(ap, fmt);
    ok = vfmt_cmd(fmt, ap);
    va_end(ap);
    if (!ok) {
        return false;
    }
    rc = system(s_cmd);
    if (rc != 0) {
        LOG_WARN("ota_bundle: command failed wait=%d exit=%d cmd=%s",
                 rc, WIFEXITED(rc) ? WEXITSTATUS(rc) : -1, s_cmd);
    }
    return rc == 0;
}

/**
 * @brief  安装单元已不在运行时清掉残留安装锁，避免开洗一直被挡
 */
static void clear_stale_install_lock(void)
{
    if (access(s_busy_path, F_OK) != 0) {
        return;
    }
    if (!fmt_cmd("systemctl is-active --quiet '%s' 2>/dev/null", s_cfg->install_unit) ||
        (system(s_cmd) == 0)) {
        return;
    }
    if (unlink(s_busy_path) == 0) {
        LOG_WARN("ota_bundle: cleared stale install lock path=%s", s_busy_path);
    }
}

sw_err_t ota_bundle_configure(const ota_bundle_cfg_t *cfg)
{
    s_cfg = NULL;
    reset_poll_state();
    if ((cfg == NULL) || !abs_path_ok(cfg->state_dir) || !abs_path_ok(cfg->work_dir) ||
        !shell_text_ok(cfg->bundle_name) || (strchr(cfg->bundle_name, '/') != NULL) ||
        !shell_text_ok(cfg->install_unit)) {
        return SW_ERR_PARAM;
    }
    if (!fill_paths(cfg)) {
        return SW_ERR_PARAM;
    }
    s_cfg = cfg;
    clear_stale_install_lock();
    return SW_OK;
}

static void set_allowed(bool allowed)
{
    FILE *fp;

    if (s_cfg == NULL) {
        return;
    }
    if (!allowed) {
        if ((unlink(s_allow_path) != 0) && (errno != ENOENT)) {
            LOG_WARN("ota_bundle: cannot clear allow ticket path=%s errno=%d", s_allow_path, errno);
        }
        return;
    }
    if ((mkdir(s_cfg->state_dir, OTA_BUNDLE_STATE_DIR_MODE) != 0) && (errno != EEXIST)) {
        LOG_WARN("ota_bundle: cannot create state dir path=%s errno=%d", s_cfg->state_dir, errno);
        return;
    }
    fp = fopen(s_allow_path, "w");
    if (fp == NULL) {
        LOG_WARN("ota_bundle: cannot write allow ticket path=%s errno=%d", s_allow_path, errno);
        return;
    }
    (void)fputs("1\n", fp);
    if (fclose(fp) != 0) {
        LOG_WARN("ota_bundle: cannot close allow ticket path=%s errno=%d", s_allow_path, errno);
    }
}

static bool is_installing(void)
{
    if (atomic_load(&s_installing)) {
        return true;
    }
    return (s_cfg != NULL) && (access(s_busy_path, F_OK) == 0);
}

static const ota_installer_ops_t s_installer_ops = {
    .set_allowed   = set_allowed,
    .is_installing = is_installing,
};

const ota_installer_ops_t *ota_bundle_installer(void)
{
    return &s_installer_ops;
}

/**
 * @brief  外层成员必须在 upgrade/ 下，禁止 `..`
 */
static bool member_allowed(const char *name)
{
    const char *p = name;

    if (strncmp(p, "./", 2) == 0) {
        p += 2;
    }
    if ((p[0] == '\0') || (strstr(p, "..") != NULL)) {
        return false;
    }
    if (strcmp(p, OTA_BUNDLE_UPGRADE_DIR_NAME) == 0) {
        return true;
    }
    return (strncmp(p, OTA_BUNDLE_UPGRADE_DIR_NAME "/", sizeof(OTA_BUNDLE_UPGRADE_DIR_NAME)) == 0);
}

/**
 * @brief  半截 gzip 列不出目录；列出后成员还须都在 upgrade/ 下
 */
static bool archive_ready(void)
{
    FILE    *fp;
    char     line[OTA_BUNDLE_MEMBER_MAX];
    unsigned members = 0U;
    size_t   len;

    if (!fmt_cmd("timeout %u tar -tzf '%s' 2>/dev/null", OTA_BUNDLE_LIST_TIMEOUT_SEC, s_bundle_path)) {
        return false;
    }
    fp = popen(s_cmd, "r");
    if (fp == NULL) {
        return false;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        len = strlen(line);
        while ((len > 0U) && ((line[len - 1U] == '\n') || (line[len - 1U] == '\r'))) {
            line[--len] = '\0';
        }
        if (len == 0U) {
            continue;
        }
        members++;
        if (!member_allowed(line)) {
            LOG_WARN("ota_bundle: archive member not allowed name=%s", line);
            (void)pclose(fp);
            return false;
        }
    }
    return (pclose(fp) == 0) && (members > 0U);
}

/**
 * @brief  取已有 sidecar 及其摘要工具（优先 SHA-256）
 * @param  tool 输出摘要命令；可为 NULL
 * @return 无 sidecar 时为 NULL
 */
static const char *sidecar_path(const char **tool)
{
    const char *path = NULL;
    const char *cmd  = NULL;

    if (access(s_sha256_path, F_OK) == 0) {
        path = s_sha256_path;
        cmd  = "sha256sum";
    } else if (access(s_md5_path, F_OK) == 0) {
        path = s_md5_path;
        cmd  = "md5sum";
    }
    if (tool != NULL) {
        *tool = cmd;
    }
    return path;
}

/**
 * @brief  无 sidecar 时按云端任务摘要补写
 */
static bool ensure_sidecar(void)
{
    if ((sidecar_path(NULL) == NULL) && (s_cfg->write_sidecar != NULL)) {
        (void)s_cfg->write_sidecar(s_bundle_path);
    }
    return sidecar_path(NULL) != NULL;
}

/**
 * @brief  用 sidecar 首字段对整包摘要
 */
static bool sidecar_matches(void)
{
    const char *tool = NULL;
    const char *path = sidecar_path(&tool);

    if ((path == NULL) ||
        !fmt_cmd("timeout %u sh -c 'e=$(awk \"{print \\$1; exit}\" \"%s\"); "
                 "h=$(%s \"%s\" | awk \"{print \\$1}\"); "
                 "[ -n \"$e\" ] && [ \"$e\" = \"$h\" ]'",
                 OTA_BUNDLE_LIST_TIMEOUT_SEC, path, tool, s_bundle_path)) {
        return false;
    }
    return system(s_cmd) == 0;
}

/**
 * @brief  解外层包并执行 upgrade.sh
 * @note   成员已由 archive_ready 检查。GNU tar 不认 --no-absolute-filenames，默认也去掉前导 /。
 */
static bool apply_bundle(void)
{
    if (!run_cmd("rm -rf '%s' && timeout %u tar -C '%s' --no-same-owner -xzf '%s'",
                 s_upgrade_dir, OTA_BUNDLE_EXTRACT_TIMEOUT_SEC, s_cfg->work_dir,
                 s_applying_path)) {
        return false;
    }
    if (!run_cmd("timeout %u bash '%s'", OTA_BUNDLE_UPGRADE_SH_TIMEOUT_SEC, s_upgrade_sh)) {
        return false;
    }
    (void)run_cmd("rm -rf '%s'", s_upgrade_dir);
    LOG_INFO("ota_bundle: handed off to %s", s_cfg->install_unit);
    return true;
}

/**
 * @brief  闩住同一份落盘，等大小变化或闸门再现
 */
static void latch(const char *why)
{
    if (!s_logged_latch) {
        LOG_WARN("ota_bundle: %s path=%s", why, s_bundle_path);
        s_logged_latch = true;
    }
    s_latched = true;
}

void ota_bundle_poll(void)
{
    struct stat st;
    bool        ok;

    if (s_cfg == NULL) {
        return;
    }
    if ((stat(s_bundle_path, &st) != 0) || (st.st_size <= 0)) {
        s_seen_size    = -1;
        s_stable_polls = 0U;
        return;
    }
    if ((int64_t)st.st_size != s_seen_size) {
        reset_poll_state();
        s_seen_size    = (int64_t)st.st_size;
        s_stable_polls = 1U;
        return;
    }
    if (s_stable_polls < OTA_BUNDLE_SIZE_STABLE_POLLS) {
        s_stable_polls++;
    }
    if (s_stable_polls < OTA_BUNDLE_SIZE_STABLE_POLLS) {
        return;
    }
    if (access(s_allow_path, F_OK) != 0) {
        s_apply_attempted = false;
        s_latched         = false;
        return;
    }
    if (s_apply_attempted || s_latched) {
        return;
    }
    if (!archive_ready()) {
        latch("archive not ready");
        return;
    }
    if (!ensure_sidecar()) {
        return;
    }
    if (!sidecar_matches()) {
        latch("sidecar mismatch");
        return;
    }
    if (rename(s_bundle_path, s_applying_path) != 0) {
        LOG_WARN("ota_bundle: cannot rename bundle errno=%d", errno);
        return;
    }
    s_apply_attempted = true;
    atomic_store(&s_installing, true);
    ok = apply_bundle();
    atomic_store(&s_installing, false);
    if (ok) {
        if ((unlink(s_applying_path) != 0) && (errno != ENOENT)) {
            LOG_WARN("ota_bundle: cannot remove applying path=%s errno=%d", s_applying_path, errno);
        }
    } else if (rename(s_applying_path, s_bundle_path) != 0) {
        LOG_WARN("ota_bundle: apply failed and cannot restore errno=%d", errno);
    } else {
        LOG_WARN("ota_bundle: apply failed, retry when allow ticket returns");
    }
}

static void ota_bundle_tick_fn(void *ctx)
{
    (void)ctx;
    ota_bundle_poll();
}

sw_err_t ota_bundle_register_task(void)
{
    return periodic_task_register("ota_bundle",
                                  OTA_BUNDLE_POLL_MS,
                                  ota_bundle_tick_fn,
                                  NULL,
                                  SCHED_OTHER,
                                  0,
                                  THD_SENSOR_POLL_STACK);
}

void ota_bundle_reset_for_test(void)
{
    s_cfg = NULL;
    atomic_store(&s_installing, false);
    reset_poll_state();
}
