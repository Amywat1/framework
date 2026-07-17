/**
 * @file    test_cloud_point_validate.c
 * @brief   cloud_point_validate 云端物模型登记期校验单元测试
 */

#include "cloud/cloud_point.h"
#include "common/sw_error.h"
#include "domain/op_mode/device_command.h"
#include "unity.h"

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

static sw_err_t svc_echo(const point_value_t *in)
{
    (void)in;
    return SW_OK;
}

static sw_err_t echo_idle(point_value_t *out)
{
    out->b = false;
    return SW_OK;
}

static cloud_point_entry_t make_telemetry_ro(const char *id)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = id;
    entry.base.type = POINT_TYPE_INT;
    entry.base.get  = get_temp;
    entry.access    = CLOUD_POINT_ACCESS_RO;
    entry.semantic  = CLOUD_POINT_SEM_TELEMETRY;
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
        make_telemetry_ro("temp"),
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_validate(entries, 1U));
}

static void test_empty_model_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(NULL, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
                          cloud_point_validate((const cloud_point_entry_t[]){make_telemetry_ro("temp")}, 0U));
}

static void test_duplicate_id_rejected(void)
{
    const cloud_point_entry_t entries[] = {
        make_telemetry_ro("temp"),
        make_telemetry_ro("temp"),
    };

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(entries, 2U));
}

static void test_ro_with_set_rejected(void)
{
    cloud_point_entry_t entry = make_telemetry_ro("temp");

    entry.base.set = set_temp;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(&entry, 1U));
}

static void test_device_cmd_missing_cmd_kind_rejected(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "stopWash";
    entry.base.type = POINT_TYPE_BOOL;
    entry.base.get  = echo_idle;
    entry.access    = CLOUD_POINT_ACCESS_WO;
    entry.semantic  = CLOUD_POINT_SEM_DEVICE_CMD;
    entry.cmd_kind  = DEV_CMD_NONE;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(&entry, 1U));
}

static void test_cloud_service_missing_handler_rejected(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "customSvc";
    entry.base.type = POINT_TYPE_BOOL;
    entry.access    = CLOUD_POINT_ACCESS_WO;
    entry.semantic  = CLOUD_POINT_SEM_CLOUD_SERVICE;
    entry.service   = NULL;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(&entry, 1U));
}

static void test_manual_act_missing_set_rejected(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "manualBrush";
    entry.base.type = POINT_TYPE_BOOL;
    entry.access    = CLOUD_POINT_ACCESS_WO;
    entry.semantic  = CLOUD_POINT_SEM_MANUAL_ACT;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_validate(&entry, 1U));
}

static void test_valid_device_cmd_and_manual_act(void)
{
    cloud_point_entry_t stop_cmd;
    cloud_point_entry_t manual_act;

    memset(&stop_cmd, 0, sizeof(stop_cmd));
    stop_cmd.base.id   = "stopWash";
    stop_cmd.base.type = POINT_TYPE_BOOL;
    stop_cmd.base.get  = echo_idle;
    stop_cmd.access    = CLOUD_POINT_ACCESS_WO;
    stop_cmd.semantic  = CLOUD_POINT_SEM_DEVICE_CMD;
    stop_cmd.cmd_kind  = DEV_CMD_STOP_WASH;

    memset(&manual_act, 0, sizeof(manual_act));
    manual_act.base.id   = "manualBrush";
    manual_act.base.type = POINT_TYPE_BOOL;
    manual_act.base.set  = set_temp;
    manual_act.access    = CLOUD_POINT_ACCESS_WO;
    manual_act.semantic  = CLOUD_POINT_SEM_MANUAL_ACT;

    const cloud_point_entry_t entries[] = {stop_cmd, manual_act};
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_validate(entries, 2U));
}

static void test_valid_cloud_service(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "customSvc";
    entry.base.type = POINT_TYPE_BOOL;
    entry.access    = CLOUD_POINT_ACCESS_WO;
    entry.semantic  = CLOUD_POINT_SEM_CLOUD_SERVICE;
    entry.service   = svc_echo;

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_validate(&entry, 1U));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_valid_telemetry_model);
    RUN_TEST(test_empty_model_rejected);
    RUN_TEST(test_duplicate_id_rejected);
    RUN_TEST(test_ro_with_set_rejected);
    RUN_TEST(test_device_cmd_missing_cmd_kind_rejected);
    RUN_TEST(test_cloud_service_missing_handler_rejected);
    RUN_TEST(test_manual_act_missing_set_rejected);
    RUN_TEST(test_valid_device_cmd_and_manual_act);
    RUN_TEST(test_valid_cloud_service);

    return UNITY_END();
}
