/**
 * @file    test_m8_cloud_model.c
 * @brief   M8 云端物模型单元测试（JSON 序列化 + 反序列化）
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/cloud/m8_cloud_register.h"
#include "framework/cloud/cloud_model.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/common/sw_version.h"
#include "third_party/cJSON/cJSON.h"
#include "unity.h"

#include <string.h>

static cmd_t s_captured_cmd;
static int   s_inject_calls;

static sw_err_t fake_inject(const cmd_t *cmd)
{
    s_captured_cmd = *cmd;
    s_inject_calls++;
    return SW_OK;
}

static const command_port_ops_t s_fake_command_ops = { .inject = fake_inject };

void setUp(void)
{
    (void)dev_ctx_init();
    command_port_register(&s_fake_command_ops);
    (void)m8_cloud_register();
    memset(&s_captured_cmd, 0, sizeof(s_captured_cmd));
    s_inject_calls = 0;
}

void tearDown(void) {}

static cJSON *build_and_parse_report(void)
{
    char buf[1024];

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_model_build_properties(buf, sizeof(buf)));
    return cJSON_Parse(buf);
}

static void test_report_process_fields(void)
{
    dev_ctx_set_operational_mode(OP_MODE_IDLE);

    cJSON *root = build_and_parse_report();
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_standby")));
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_stopping")));
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_dev_warning")));

    cJSON_Delete(root);
}

static void test_report_stopping_covers_fault(void)
{
    dev_ctx_set_operational_mode(OP_MODE_EXCEPTION);

    cJSON *root = build_and_parse_report();
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_stopping")));
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_standby")));

    cJSON_Delete(root);
}

static void test_report_monitor_fields(void)
{
    dev_ctx_set_gantry_pos(1234);

    cJSON *root = build_and_parse_report();
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_EQUAL_INT(1234, (int)cJSON_GetObjectItem(root, "sts_gantry_position")->valuedouble);
    TEST_ASSERT_EQUAL_STRING(SW_VERSION_STR, cJSON_GetObjectItem(root, "sts_firmware_version")->valuestring);
    TEST_ASSERT_EQUAL_STRING(SW_PRODUCT_NAME, cJSON_GetObjectItem(root, "sts_device_model")->valuestring);

    cJSON_Delete(root);
}

static void test_dispatch_cmd_home(void)
{
    (void)cloud_model_apply_property_set("{\"cmd_home\":1}", NULL);

    TEST_ASSERT_EQUAL_INT(1, s_inject_calls);
    TEST_ASSERT_EQUAL_INT(CMD_HOME_DEVICE, s_captured_cmd.type);
}

static void test_dispatch_write_zero_is_noop(void)
{
    (void)cloud_model_apply_property_set("{\"cmd_home\":0}", NULL);

    TEST_ASSERT_EQUAL_INT(0, s_inject_calls);
}

static void test_dispatch_multi_property_and_unknown_id(void)
{
    (void)cloud_model_apply_property_set(
        "{\"cmd_communication_test\":1,\"unknown_point_xyz\":1,\"cmd_custom_stop\":1}",
        NULL);

    TEST_ASSERT_EQUAL_INT(1, s_inject_calls);
    TEST_ASSERT_EQUAL_INT(CMD_STOP_WASH, s_captured_cmd.type);

    cJSON *root = build_and_parse_report();
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_communication_test")));
    cJSON_Delete(root);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_report_process_fields);
    RUN_TEST(test_report_stopping_covers_fault);
    RUN_TEST(test_report_monitor_fields);
    RUN_TEST(test_dispatch_cmd_home);
    RUN_TEST(test_dispatch_write_zero_is_noop);
    RUN_TEST(test_dispatch_multi_property_and_unknown_id);
    return UNITY_END();
}
