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
static dev_cmd_kind_t s_last_cmd;

static sw_err_t get_speed(point_value_t *out)
{
    out->i = s_speed;
    return SW_OK;
}

static sw_err_t set_manual(const point_value_t *in)
{
    s_manual_on = in->b;
    return SW_OK;
}

static sw_err_t submit_device_cmd(dev_cmd_kind_t kind)
{
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
    return entry;
}

static cloud_point_entry_t make_write(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "manualBrush";
    entry.base.type = POINT_TYPE_BOOL;
    entry.base.set  = set_manual;
    entry.kind      = CLOUD_KIND_WRITE;
    return entry;
}

static cloud_point_entry_t make_stop_cmd(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "stopWash";
    entry.base.type = POINT_TYPE_BOOL;
    entry.base.get  = cloud_point_get_echo_idle;
    entry.kind      = CLOUD_KIND_COMMAND;
    entry.cmd_kind  = DEV_CMD_STOP_WASH;
    return entry;
}

void setUp(void)
{
    s_speed     = 120;
    s_manual_on = false;
    s_last_cmd  = DEV_CMD_NONE;
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

static void test_to_json_serializes_telemetry(void)
{
    char                      buf[64];
    const cloud_point_entry_t entries[] = {make_speed_telemetry()};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_to_json(entries, 1U, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"speed\":120"));
}

static void test_apply_json_write(void)
{
    point_apply_result_t      result;
    const cloud_point_entry_t entries[] = {make_write()};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_apply_json(entries, 1U, "{\"manualBrush\":true}", &result));
    TEST_ASSERT_EQUAL_UINT(1U, result.applied);
    TEST_ASSERT_TRUE(s_manual_on);
}

static void test_apply_json_command_triggers_submit(void)
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

static void test_apply_json_command_false_is_noop(void)
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
    const cloud_point_entry_t entries[] = {make_write()};

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_apply_json(entries, 1U, "{\"unknown\":1,\"manualBrush\":true}", &result));
    TEST_ASSERT_EQUAL_UINT(2U, result.total_keys);
    TEST_ASSERT_EQUAL_UINT(1U, result.applied);
    TEST_ASSERT_EQUAL_UINT(1U, result.rejected);
    TEST_ASSERT_TRUE(s_manual_on);
}

static void test_apply_json_rejects_non_object(void)
{
    point_apply_result_t      result;
    const cloud_point_entry_t entries[] = {make_write()};

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

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_get_echo_idle_returns_false, "", "验证获取回显空闲模式返回false");
    WDF_RUN_TEST(test_to_json_serializes_telemetry, "", "验证将遥测点位序列化为 JSON");
    WDF_RUN_TEST(test_apply_json_write, "", "验证应用 JSON 写入点位");
    WDF_RUN_TEST(test_apply_json_command_triggers_submit, "", "验证应用 JSON 命令触发提交");
    WDF_RUN_TEST(test_apply_json_rejects_telemetry, "", "验证应用 JSON 拒绝遥测写入");
    WDF_RUN_TEST(test_apply_json_command_false_is_noop, "", "验证应用 JSON 命令 false 为无操作");
    WDF_RUN_TEST(test_apply_json_unknown_key_partial_reject, "", "验证应用 JSON 未知键部分拒绝");
    WDF_RUN_TEST(test_apply_json_rejects_non_object, "", "验证应用 JSON 拒绝非对象根");
    WDF_RUN_TEST(test_to_json_filtered_selects_id, "", "验证过滤序列化时只输出指定点位 ID");

    return UNITY_END();
}
