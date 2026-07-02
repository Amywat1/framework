/**
 * @file    test_m8_tsl_table.c
 * @brief   M8 物模型点位表单元测试（上行序列化 + 下行分发）
 * @author  HUWANGWEI
 * @date    2026-07-02
 */

#include "machines/m8/adapters/cloud/m8_tsl_table.h"
#include "infrastructure/services/dev_ctx/dev_ctx.h"
#include "ports/cloud/command_port.h"
#include "common/sw_version.h"
#include "third_party/cJSON/cJSON.h"
#include "unity.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * command_port 假实现：记录最近一次 inject 的 cmd_t
 * ------------------------------------------------------------------------- */
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
    memset(&s_captured_cmd, 0, sizeof(s_captured_cmd));
    s_inject_calls = 0;
}

void tearDown(void) {}

/* -------------------------------------------------------------------------
 * 辅助：构建上报 JSON 并解析为 cJSON 对象（调用方负责 cJSON_Delete）
 * ------------------------------------------------------------------------- */
static cJSON *build_and_parse_report(void)
{
    cloud_report_payload_t dummy;
    char                    buf[1024];

    memset(&dummy, 0, sizeof(dummy));
    TEST_ASSERT_EQUAL_INT(SW_OK, m8_build_report_json(&dummy, buf, sizeof(buf)));
    return cJSON_Parse(buf);
}

/* -------------------------------------------------------------------------
 * TC-1：待机状态下，洗车进程派生字段正确
 * ------------------------------------------------------------------------- */
static void test_report_process_fields(void)
{
    dev_ctx_set_device_state(DEV_STATE_IDLE);

    cJSON *root = build_and_parse_report();
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_standby")));
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_stopping")));
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_dev_warning")));

    cJSON_Delete(root);
}

static void test_report_stopping_covers_fault(void)
{
    dev_ctx_set_device_state(DEV_STATE_FAULT);

    cJSON *root = build_and_parse_report();
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_stopping")));
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "sts_standby")));

    cJSON_Delete(root);
}

/* -------------------------------------------------------------------------
 * TC-2：监测数据字段（龙门位置 / 固件版本 / 设备型号）
 * ------------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------
 * TC-3：下行 cmd_home 写 1 触发 command_port.inject(CMD_HOME_DEVICE)
 * ------------------------------------------------------------------------- */
static void test_dispatch_cmd_home(void)
{
    m8_tsl_command_dispatch("{\"cmd_home\":1}");

    TEST_ASSERT_EQUAL_INT(1, s_inject_calls);
    TEST_ASSERT_EQUAL_INT(CMD_HOME_DEVICE, s_captured_cmd.type);
}

/* -------------------------------------------------------------------------
 * TC-4：写 0 不下发指令
 * ------------------------------------------------------------------------- */
static void test_dispatch_write_zero_is_noop(void)
{
    m8_tsl_command_dispatch("{\"cmd_home\":0}");

    TEST_ASSERT_EQUAL_INT(0, s_inject_calls);
}

/* -------------------------------------------------------------------------
 * TC-5：一条消息同时携带多个属性，且未知标识符不影响其它属性处理
 * ------------------------------------------------------------------------- */
static void test_dispatch_multi_property_and_unknown_id(void)
{
    m8_tsl_command_dispatch("{\"cmd_communication_test\":1,\"unknown_point_xyz\":1,\"cmd_custom_stop\":1}");

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
