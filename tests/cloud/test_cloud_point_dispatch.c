/**
 * @file    test_cloud_point_dispatch.c
 * @brief   cloud_point dispatch 与 JSON 编解码单元测试
 */

#include "adapters/outbound/cloud/cloud_point_json.h"
#include "common/sw_error.h"
#include "domain/cloud/cloud_point.h"
#include "domain/op_mode/device_command.h"
#include "wdf_test_spec.h"

#include <string.h>

static int32_t        s_speed;
static bool           s_manual_on;
static bool           s_write_fail;
static bool           s_cmd_fail;
static dev_cmd_kind_t s_last_cmd;

static sw_err_t get_speed(point_value_t *out)
{
    out->i = s_speed;
    return SW_OK;
}

static sw_err_t get_manual(point_value_t *out)
{
    out->b = s_manual_on;
    return SW_OK;
}

static sw_err_t set_manual(const point_value_t *in)
{
    if (s_write_fail) {
        return SW_ERR_STATE;
    }
    s_manual_on = in->b;
    return SW_OK;
}

static sw_err_t submit_device_cmd(dev_cmd_kind_t kind)
{
    if (s_cmd_fail) {
        return SW_ERR_STATE;
    }
    s_last_cmd = kind;
    return SW_OK;
}

static cloud_point_entry_t make_speed_telemetry(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "speed";
    entry.base.type = POINT_TYPE_INT;
    entry.base.get  = get_speed;
    entry.kind      = CLOUD_KIND_TELEMETRY;
    entry.report    = CLOUD_REPORT_ON_CHANGE;
    return entry;
}

static cloud_point_entry_t make_set(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "manualBrush";
    entry.base.type = POINT_TYPE_BOOL;
    entry.base.get  = get_manual;
    entry.base.set  = set_manual;
    entry.kind      = CLOUD_KIND_SET;
    entry.report    = CLOUD_REPORT_ON_CHANGE;
    return entry;
}

static cloud_point_entry_t make_stop_cmd(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "stopWash";
    entry.base.type = POINT_TYPE_BOOL;
    entry.base.get  = cloud_point_get_echo_idle;
    entry.kind      = CLOUD_KIND_DEV_CMD;
    entry.report    = CLOUD_REPORT_RESYNC;
    entry.cmd_kind  = DEV_CMD_STOP_WASH;
    return entry;
}

void setUp(void)
{
    s_speed      = 120;
    s_manual_on  = false;
    s_write_fail = false;
    s_cmd_fail   = false;
    s_last_cmd   = DEV_CMD_NONE;
    cloud_point_set_device_cmd_submit(submit_device_cmd);
}

void tearDown(void)
{
}

static void test_get_echo_idle_returns_false(void)
{
    point_value_t val;

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_get_echo_idle(&val));
    TEST_ASSERT_FALSE(val.b);
}

static void test_set_resync_is_pulse(void)
{
    cloud_point_entry_t hold  = make_set();
    cloud_point_entry_t pulse = make_set();
    cloud_point_entry_t cmd   = make_stop_cmd();

    TEST_ASSERT_FALSE(cloud_point_is_pulse(&hold));
    pulse.report = CLOUD_REPORT_RESYNC;
    TEST_ASSERT_TRUE(cloud_point_is_pulse(&pulse));
    TEST_ASSERT_TRUE(cloud_point_is_pulse(&cmd));
}

static void test_to_json_serializes_telemetry(void)
{
    char                      buf[64];
    const cloud_point_entry_t entries[] = {make_speed_telemetry()};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_to_json(entries, 1U, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"speed\":120"));
}

static sw_err_t get_fw(point_value_t *out)
{
    (void)strncpy(out->s, "1.0", POINT_STR_MAX - 1U);
    out->s[POINT_STR_MAX - 1U] = '\0';
    return SW_OK;
}

static void test_to_json_omits_none(void)
{
    char                buf[64];
    cloud_point_entry_t entry = make_speed_telemetry();

    entry.report = CLOUD_REPORT_NONE;
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_to_json(&entry, 1U, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("{}", buf);
}

static void test_to_json_snapshot_includes_resync_omits_none(void)
{
    char                buf[128];
    cloud_point_entry_t change = make_speed_telemetry();
    cloud_point_entry_t resync;
    cloud_point_entry_t cmd    = make_stop_cmd();
    cloud_point_entry_t none   = make_set();

    memset(&resync, 0, sizeof(resync));
    resync.base.id   = "fw";
    resync.base.type = POINT_TYPE_STRING;
    resync.base.get  = get_fw;
    resync.kind      = CLOUD_KIND_TELEMETRY;
    resync.report    = CLOUD_REPORT_RESYNC;

    none.base.id = "monitor";
    none.report  = CLOUD_REPORT_NONE;

    const cloud_point_entry_t entries[] = {change, resync, cmd, none};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_to_json(entries, 4U, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"speed\":120"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"fw\":\"1.0\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"stopWash\":0"));
    TEST_ASSERT_NULL(strstr(buf, "monitor"));
}

static void test_apply_json_set(void)
{
    point_apply_result_t      result;
    const cloud_point_entry_t entries[] = {make_set()};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_apply_json(entries, 1U, "{\"manualBrush\":true}", &result));
    TEST_ASSERT_EQUAL_UINT(1U, result.applied);
    TEST_ASSERT_TRUE(s_manual_on);
}

static void test_apply_json_dev_cmd_triggers_submit(void)
{
    point_apply_result_t      result;
    const cloud_point_entry_t entries[] = {make_stop_cmd()};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_apply_json(entries, 1U, "{\"stopWash\":true}", &result));
    TEST_ASSERT_EQUAL_UINT(1U, result.applied);
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STOP_WASH, s_last_cmd);
}

static void test_apply_json_rejects_telemetry(void)
{
    point_apply_result_t      result;
    const cloud_point_entry_t entries[] = {make_speed_telemetry()};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_apply_json(entries, 1U, "{\"speed\":200}", &result));
    TEST_ASSERT_EQUAL_UINT(1U, result.rejected);
    TEST_ASSERT_EQUAL_INT(120, s_speed);
}

static void test_apply_json_dev_cmd_false_is_noop(void)
{
    point_apply_result_t      result;
    const cloud_point_entry_t entries[] = {make_stop_cmd()};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_apply_json(entries, 1U, "{\"stopWash\":false}", &result));
    TEST_ASSERT_EQUAL_UINT(1U, result.applied);
    TEST_ASSERT_EQUAL_INT(DEV_CMD_NONE, s_last_cmd);
}

static void test_apply_json_unknown_key_partial_reject(void)
{
    point_apply_result_t      result;
    const cloud_point_entry_t entries[] = {make_set()};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_apply_json(entries, 1U, "{\"unknown\":1,\"manualBrush\":true}", &result));
    TEST_ASSERT_EQUAL_UINT(2U, result.total_keys);
    TEST_ASSERT_EQUAL_UINT(1U, result.applied);
    TEST_ASSERT_EQUAL_UINT(1U, result.rejected);
    TEST_ASSERT_TRUE(s_manual_on);
}

static void test_apply_json_rejects_non_object(void)
{
    point_apply_result_t      result;
    const cloud_point_entry_t entries[] = {make_set()};

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_apply_json(entries, 1U, "[1,2]", &result));
}

static void test_to_json_filtered_selects_id(void)
{
    char                      buf[64];
    const char               *ids[]     = {"speed"};
    const cloud_point_entry_t entries[] = {make_speed_telemetry()};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_to_json_filtered(entries, 1U, ids, 1U, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"speed\":120"));
}

static void test_to_json_filtered_omits_none(void)
{
    char                buf[64];
    const char         *ids[]   = {"speed", "monitor"};
    cloud_point_entry_t missing = make_set();

    missing.base.id = "monitor";
    missing.report  = CLOUD_REPORT_NONE;

    const cloud_point_entry_t entries[] = {make_speed_telemetry(), missing};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_to_json_filtered(entries, 2U, ids, 2U, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"speed\":120"));
    TEST_ASSERT_NULL(strstr(buf, "monitor"));
}

static void test_to_json_echo_sent_values(void)
{
    char                 echo[192];
    char                 idle[192];
    point_apply_result_t result;
    cloud_point_entry_t  cmd     = make_stop_cmd();
    cloud_point_entry_t  write   = make_set();
    cloud_point_entry_t  pulse   = make_set();
    cloud_point_entry_t  missing = make_set();
    const char          *req     = "{\"stopWash\":true,\"manualBrush\":true,\"cmdStop\":true,\"monitor\":true}";

    pulse.base.id  = "cmdStop";
    pulse.base.get = cloud_point_get_echo_idle;
    pulse.report   = CLOUD_REPORT_RESYNC;

    missing.base.id = "monitor";
    missing.report  = CLOUD_REPORT_NONE;

    const cloud_point_entry_t entries[] = {cmd, write, pulse, missing};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_apply_json(entries, 4U, req, &result));
    TEST_ASSERT_EQUAL_UINT(4U, result.applied);
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_to_json_downlink(entries, 4U, req, result.applied_ids,
                                                             result.applied_id_count, echo, sizeof(echo), idle,
                                                             sizeof(idle)));
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"stopWash\":1"));
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"manualBrush\":1"));
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"cmdStop\":1"));
    TEST_ASSERT_NULL(strstr(echo, "monitor"));
    TEST_ASSERT_NOT_NULL(strstr(idle, "\"stopWash\":0"));
    TEST_ASSERT_NOT_NULL(strstr(idle, "\"cmdStop\":0"));
    TEST_ASSERT_NULL(strstr(idle, "manualBrush"));
    TEST_ASSERT_NULL(strstr(idle, "monitor"));
}

static void test_to_json_echo_rejected_reports_current(void)
{
    char                 echo[192];
    char                 idle[192];
    point_apply_result_t result;
    cloud_point_entry_t  cmd     = make_stop_cmd();
    cloud_point_entry_t  write   = make_set();
    cloud_point_entry_t  telem   = make_speed_telemetry();
    cloud_point_entry_t  missing = make_set();
    const char          *req     = "{\"stopWash\":true,\"manualBrush\":true,\"speed\":9,\"monitor\":true}";

    s_cmd_fail   = true;
    s_write_fail = true;
    missing.base.id = "monitor";
    missing.report  = CLOUD_REPORT_NONE;

    const cloud_point_entry_t entries[] = {cmd, write, telem, missing};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_apply_json(entries, 4U, req, &result));
    TEST_ASSERT_EQUAL_UINT(0U, result.applied);
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_to_json_downlink(entries, 4U, req, result.applied_ids,
                                                             result.applied_id_count, echo, sizeof(echo), idle,
                                                             sizeof(idle)));
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"stopWash\":0"));
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"manualBrush\":0"));
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"speed\":120"));
    TEST_ASSERT_NULL(strstr(echo, "monitor"));
    TEST_ASSERT_FALSE(s_manual_on);
    TEST_ASSERT_EQUAL_STRING("{}", idle);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_get_echo_idle_returns_false, "", "验证获取回显空闲模式返回false");
    WDF_RUN_TEST(test_set_resync_is_pulse, "", "验证 SET 的 RESYNC 即脉冲，不依赖 getter");
    WDF_RUN_TEST(test_to_json_serializes_telemetry, "", "验证将遥测点位序列化为 JSON");
    WDF_RUN_TEST(test_to_json_omits_none, "", "验证 NONE 策略点位不进入快照 JSON");
    WDF_RUN_TEST(test_to_json_snapshot_includes_resync_omits_none, "", "验证快照含 RESYNC 且不含 NONE");
    WDF_RUN_TEST(test_apply_json_set, "", "验证应用 JSON 点位写入");
    WDF_RUN_TEST(test_apply_json_dev_cmd_triggers_submit, "", "验证应用 JSON 设备命令触发提交");
    WDF_RUN_TEST(test_apply_json_rejects_telemetry, "", "验证应用 JSON 拒绝遥测写入");
    WDF_RUN_TEST(test_apply_json_dev_cmd_false_is_noop, "", "验证应用 JSON 设备命令 false 为无操作");
    WDF_RUN_TEST(test_apply_json_unknown_key_partial_reject, "", "验证应用 JSON 未知键部分拒绝");
    WDF_RUN_TEST(test_apply_json_rejects_non_object, "", "验证应用 JSON 拒绝非对象根");
    WDF_RUN_TEST(test_to_json_filtered_selects_id, "", "验证过滤序列化时只输出指定点位 ID");
    WDF_RUN_TEST(test_to_json_filtered_omits_none, "", "验证增量组包过滤 NONE 点位");
    WDF_RUN_TEST(test_to_json_echo_sent_values, "", "验证下行回显 1 后脉冲再报 0，跳过未进物模型点");
    WDF_RUN_TEST(test_to_json_echo_rejected_reports_current, "", "验证下行失败立刻上报当前真实值");

    return UNITY_END();
}
