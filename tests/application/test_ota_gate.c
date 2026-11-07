/**
 * @file    test_ota_gate.c
 * @brief   远程升级门闩：允许态、一次上报、闸门跟随与开洗互斥
 */

#include "application/ota_gate.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "common/log.h"
#include "domain/op_mode/operational_mode.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

#include <stdio.h>
#include <string.h>

static bool               s_online;
static bool               s_estop;
static operational_mode_t s_mode;
static sw_err_t           s_report_ret;
static unsigned           s_report_calls;
static char               s_report_version[32];
static bool               s_channel_busy;
static bool               s_allowed;
static unsigned           s_set_allowed_calls;
static bool               s_installing;
static unsigned           s_subscribe_calls;

static bool stub_online(void)
{
    return s_online;
}

static const cloud_link_ops_t s_link_ops = {
    .is_online = stub_online,
};

const cloud_link_ops_t *cloud_link_get_ops(void)
{
    return &s_link_ops;
}

operational_mode_t op_mode_get_current(void)
{
    return s_mode;
}

bool op_mode_is_estop_active(void)
{
    return s_estop;
}

sw_err_t event_subscribe_table(const event_subscription_t *subs, size_t count)
{
    (void)subs;
    (void)count;
    s_subscribe_calls++;
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

static sw_err_t stub_report(const char *version)
{
    s_report_calls++;
    (void)snprintf(s_report_version, sizeof(s_report_version), "%s", version);
    return s_report_ret;
}

static bool stub_channel_busy(void)
{
    return s_channel_busy;
}

static void stub_set_allowed(bool allowed)
{
    s_set_allowed_calls++;
    s_allowed = allowed;
}

static bool stub_installing(void)
{
    return s_installing;
}

static const ota_channel_ops_t s_channel = {
    .report = stub_report,
    .busy   = stub_channel_busy,
};

static const ota_installer_ops_t s_installer = {
    .set_allowed   = stub_set_allowed,
    .is_installing = stub_installing,
};

static const ota_gate_cfg_t s_cfg = {
    .version   = "v1.2.3",
    .channel   = &s_channel,
    .installer = &s_installer,
};

static const ota_gate_cfg_t s_cfg_no_channel = {
    .installer = &s_installer,
};

void setUp(void)
{
    s_online            = true;
    s_estop             = false;
    s_mode              = OP_MODE_STANDBY;
    s_report_ret        = SW_OK;
    s_report_calls      = 0U;
    s_report_version[0] = '\0';
    s_channel_busy      = false;
    s_allowed           = false;
    s_set_allowed_calls = 0U;
    s_installing        = false;
    s_subscribe_calls   = 0U;
    ota_gate_reset_for_test();
    TEST_ASSERT_EQUAL_INT(SW_OK, ota_gate_bind(&s_cfg));
}

void tearDown(void)
{
    ota_gate_reset_for_test();
}

static void test_allowed_modes(void)
{
    TEST_ASSERT_TRUE(ota_gate_is_allowed(OP_MODE_STANDBY, false));
    TEST_ASSERT_TRUE(ota_gate_is_allowed(OP_MODE_STOPPED, false));
    TEST_ASSERT_FALSE(ota_gate_is_allowed(OP_MODE_STANDBY, true));
    TEST_ASSERT_FALSE(ota_gate_is_allowed(OP_MODE_STOPPED, true));
    TEST_ASSERT_FALSE(ota_gate_is_allowed(OP_MODE_HOLD, false));
    TEST_ASSERT_FALSE(ota_gate_is_allowed(OP_MODE_ACTIVE, false));
    TEST_ASSERT_FALSE(ota_gate_is_allowed(OP_MODE_CLEARING, false));
    TEST_ASSERT_FALSE(ota_gate_is_allowed(OP_MODE_RECOVERING, false));
    TEST_ASSERT_FALSE(ota_gate_is_allowed(OP_MODE_SELF_CHECK, false));
    TEST_ASSERT_FALSE(ota_gate_is_allowed(OP_MODE_INIT, false));
}

static void test_report_once_and_again_after_leaving(void)
{
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(1U, s_report_calls);
    TEST_ASSERT_EQUAL_STRING("v1.2.3", s_report_version);
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(1U, s_report_calls);
    s_mode = OP_MODE_HOLD;
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(1U, s_report_calls);
    s_mode = OP_MODE_STANDBY;
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(2U, s_report_calls);
}

static void test_report_failure_retries(void)
{
    s_report_ret = SW_ERR_COMM;
    ota_gate_sync();
    s_report_ret = SW_OK;
    ota_gate_sync();
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(2U, s_report_calls);
}

static void test_denied_modes_do_not_report(void)
{
    s_mode = OP_MODE_HOLD;
    ota_gate_sync();
    s_mode  = OP_MODE_STOPPED;
    s_estop = true;
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(0U, s_report_calls);
    TEST_ASSERT_FALSE(s_allowed);
}

static void test_offline_does_not_report(void)
{
    s_online = false;
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(0U, s_report_calls);
    s_online = true;
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(1U, s_report_calls);
}

static void test_offline_clears_reported_and_reports_again(void)
{
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(1U, s_report_calls);
    s_online = false;
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(1U, s_report_calls);
    s_online = true;
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(2U, s_report_calls);
}

static void test_ticket_follows_allowed_state_only(void)
{
    s_online = false;
    ota_gate_sync();
    TEST_ASSERT_TRUE(s_allowed);
    s_mode = OP_MODE_ACTIVE;
    ota_gate_sync();
    TEST_ASSERT_FALSE(s_allowed);
    TEST_ASSERT_EQUAL_INT(SW_OK, ota_gate_bind(&s_cfg_no_channel));
    s_mode = OP_MODE_STANDBY;
    ota_gate_sync();
    TEST_ASSERT_TRUE(s_allowed);
    TEST_ASSERT_EQUAL_UINT(0U, s_report_calls);
}

static void test_blocks_start(void)
{
    TEST_ASSERT_FALSE(ota_gate_blocks_start());
    s_channel_busy = true;
    TEST_ASSERT_TRUE(ota_gate_blocks_start());
    s_channel_busy = false;
    s_installing   = true;
    TEST_ASSERT_TRUE(ota_gate_blocks_start());
    ota_gate_reset_for_test();
    TEST_ASSERT_FALSE(ota_gate_blocks_start());
}

static void test_bind_rejects_incomplete(void)
{
    static const ota_channel_ops_t   no_report  = {.busy = stub_channel_busy};
    static const ota_installer_ops_t half       = {.set_allowed = stub_set_allowed};
    static const ota_gate_cfg_t      bad_ch     = {.version = "v1", .channel = &no_report};
    static const ota_gate_cfg_t      no_version = {.channel = &s_channel};
    static const ota_gate_cfg_t      bad_inst   = {.installer = &half};

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, ota_gate_bind(&bad_ch));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, ota_gate_bind(&no_version));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, ota_gate_bind(&bad_inst));
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(1U, s_report_calls);
    TEST_ASSERT_EQUAL_INT(SW_OK, ota_gate_bind(NULL));
    ota_gate_sync();
    TEST_ASSERT_EQUAL_UINT(1U, s_report_calls);
}

static void test_init_subscribes_once_and_syncs(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, ota_gate_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, ota_gate_init());
    TEST_ASSERT_EQUAL_UINT(1U, s_subscribe_calls);
    TEST_ASSERT_EQUAL_UINT(2U, s_set_allowed_calls);
    TEST_ASSERT_EQUAL_UINT(1U, s_report_calls);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_allowed_modes, "UPG-01", "仅 STANDBY 或无急停 STOPPED 为允许态");
    WDF_RUN_TEST(test_report_once_and_again_after_leaving, "UPG-02",
                 "在线允许态只报一次，离开后再进入重报");
    WDF_RUN_TEST(test_report_failure_retries, "UPG-02", "上报失败下次同步再试");
    WDF_RUN_TEST(test_denied_modes_do_not_report, "UPG-01", "禁止态与急停不报版本、撤闸门");
    WDF_RUN_TEST(test_offline_does_not_report, "UPG-02", "离线不报，上线后再报");
    WDF_RUN_TEST(test_offline_clears_reported_and_reports_again, "UPG-02",
                 "已上报后离线再上线，仍在待机也会再报");
    WDF_RUN_TEST(test_ticket_follows_allowed_state_only, "UPG-03",
                 "闸门只跟允许态，与云在线和通道绑定无关");
    WDF_RUN_TEST(test_blocks_start, "UPG-04", "通道下载忙或安装中拒绝开洗");
    WDF_RUN_TEST(test_bind_rejects_incomplete, "", "缺必填回调或版本串拒绝绑定，保持原绑定");
    WDF_RUN_TEST(test_init_subscribes_once_and_syncs, "", "重复 init 只订阅一次并同步");
    return UNITY_END();
}
