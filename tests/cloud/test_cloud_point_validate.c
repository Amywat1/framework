/**
 * @file    test_cloud_point_validate.c
 * @brief   cloud_point_validate 云端物模型登记期校验单元测试
 */

#include "common/sw_error.h"
#include "domain/cloud/cloud_point.h"
#include "domain/op_mode/device_command.h"
#include "wdf_test_spec.h"

#include <string.h>

static int32_t s_temp;

static sw_err_t get_temp(point_value_t *out)
{
    out->i = s_temp;
    return SW_OK;
}

static sw_err_t set_temp(const point_value_t *in)
{
    s_temp = in->i;
    return SW_OK;
}

static sw_err_t echo_idle(point_value_t *out)
{
    out->b = false;
    return SW_OK;
}

static cloud_point_entry_t make_telemetry(const char *id)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = id;
    entry.base.type = POINT_TYPE_INT;
    entry.base.get  = get_temp;
    entry.kind      = CLOUD_KIND_TELEMETRY;
    return entry;
}

void setUp(void)
{
    s_temp = 25;
}

void tearDown(void)
{
}

static void test_valid_telemetry_model(void)
{
    const cloud_point_entry_t entries[] = {
        make_telemetry("temp"),
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_validate(entries, 1U));
}

static void test_empty_model_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(NULL, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
                          cloud_point_validate((const cloud_point_entry_t[]){make_telemetry("temp")}, 0U));
}

static void test_duplicate_id_rejected(void)
{
    const cloud_point_entry_t entries[] = {
        make_telemetry("temp"),
        make_telemetry("temp"),
    };

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(entries, 2U));
}

static void test_telemetry_with_set_rejected(void)
{
    cloud_point_entry_t entry = make_telemetry("temp");

    entry.base.set = set_temp;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(&entry, 1U));
}

static void test_command_missing_cmd_kind_rejected(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "stopWash";
    entry.base.type = POINT_TYPE_BOOL;
    entry.base.get  = echo_idle;
    entry.kind      = CLOUD_KIND_COMMAND;
    entry.cmd_kind  = DEV_CMD_NONE;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(&entry, 1U));
}

static void test_command_non_bool_rejected(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "stopWash";
    entry.base.type = POINT_TYPE_INT;
    entry.kind      = CLOUD_KIND_COMMAND;
    entry.cmd_kind  = DEV_CMD_STOP_WASH;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(&entry, 1U));
}

static void test_write_missing_set_rejected(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "manualBrush";
    entry.base.type = POINT_TYPE_BOOL;
    entry.kind      = CLOUD_KIND_WRITE;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(&entry, 1U));
}

static void test_on_change_float_rejected(void)
{
    cloud_point_entry_t entry = make_telemetry("flow");

    entry.base.type = POINT_TYPE_FLOAT;
    entry.on_change = true;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(&entry, 1U));
}

static void test_valid_command_and_write(void)
{
    cloud_point_entry_t stop_cmd;
    cloud_point_entry_t write_act;

    memset(&stop_cmd, 0, sizeof(stop_cmd));
    stop_cmd.base.id   = "stopWash";
    stop_cmd.base.type = POINT_TYPE_BOOL;
    stop_cmd.base.get  = echo_idle;
    stop_cmd.kind      = CLOUD_KIND_COMMAND;
    stop_cmd.cmd_kind  = DEV_CMD_STOP_WASH;

    memset(&write_act, 0, sizeof(write_act));
    write_act.base.id   = "manualBrush";
    write_act.base.type = POINT_TYPE_BOOL;
    write_act.base.set  = set_temp;
    write_act.kind      = CLOUD_KIND_WRITE;

    const cloud_point_entry_t entries[] = {stop_cmd, write_act};
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_validate(entries, 2U));
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_valid_telemetry_model, "", "验证有效遥测模型");
    WDF_RUN_TEST(test_empty_model_rejected, "", "验证空模型被拒绝");
    WDF_RUN_TEST(test_duplicate_id_rejected, "", "验证重复ID被拒绝");
    WDF_RUN_TEST(test_telemetry_with_set_rejected, "", "验证遥测点位配置写入接口时被拒绝");
    WDF_RUN_TEST(test_command_missing_cmd_kind_rejected, "", "验证命令缺失命令类型被拒绝");
    WDF_RUN_TEST(test_command_non_bool_rejected, "", "验证非 bool 命令被拒绝");
    WDF_RUN_TEST(test_write_missing_set_rejected, "", "验证写入点位缺失 set 被拒绝");
    WDF_RUN_TEST(test_on_change_float_rejected, "", "验证 on_change 的 FLOAT 点位被拒绝");
    WDF_RUN_TEST(test_valid_command_and_write, "", "验证命令和写入模型配置有效");

    return UNITY_END();
}
