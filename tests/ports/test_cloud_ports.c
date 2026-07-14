/**
 * @file    test_cloud_ports.c
 * @brief   cloud 端口 register/get 单元测试
 */

#include "common/sw_error.h"
#include "domain/command_gateway/command_types.h"
#include "domain/command_gateway/device_command.h"
#include "ports/inbound/cloud/property/property_port.h"
#include "ports/inbound/command/command_port.h"
#include "ports/outbound/cloud/link/cloud_link_port.h"
#include "ports/outbound/cloud/report/report_port.h"
#include "unity.h"

static sw_err_t stub_publish_properties(void)
{
    return SW_OK;
}

static sw_err_t stub_publish_delta(const char *const *ids, size_t count)
{
    (void)ids;
    (void)count;
    return SW_OK;
}

static bool stub_is_online(void)
{
    return true;
}

static sw_err_t stub_on_property_set(const char *json_payload, point_apply_result_t *result)
{
    (void)json_payload;
    (void)result;
    return SW_OK;
}

static const cloud_report_ops_t s_report_ops = {
    .publish_properties       = stub_publish_properties,
    .publish_properties_delta = stub_publish_delta,
};

static const cloud_link_ops_t s_link_ops = {
    .is_online = stub_is_online,
};

static const cloud_property_ops_t s_property_ops = {
    .on_property_set = stub_on_property_set,
};

static sw_err_t stub_submit(const dev_cmd_t *cmd, dev_cmd_receipt_t *receipt, uint32_t timeout_ms)
{
    (void)cmd;
    (void)timeout_ms;
    if (receipt != NULL) {
        receipt->status = DEV_CMD_STATUS_ACCEPTED;
    }
    return SW_OK;
}

static const device_command_port_ops_t s_command_ops = {
    .submit = stub_submit,
};

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_cloud_report_register_and_get(void)
{
    TEST_ASSERT_NULL(cloud_report_get_ops());
    cloud_report_register(&s_report_ops);
    TEST_ASSERT_EQUAL_PTR(&s_report_ops, cloud_report_get_ops());
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_report_get_ops()->publish_properties());
}

static void test_cloud_link_register_and_get(void)
{
    TEST_ASSERT_NULL(cloud_link_get_ops());
    cloud_link_register(&s_link_ops);
    TEST_ASSERT_EQUAL_PTR(&s_link_ops, cloud_link_get_ops());
    TEST_ASSERT_TRUE(cloud_link_get_ops()->is_online());
}

static void test_cloud_property_register_and_get(void)
{
    TEST_ASSERT_NULL(cloud_property_get_ops());
    cloud_property_register(&s_property_ops);
    TEST_ASSERT_EQUAL_PTR(&s_property_ops, cloud_property_get_ops());
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_property_get_ops()->on_property_set("{}", NULL));
}

static void test_device_command_port_register_and_get(void)
{
    dev_cmd_t         cmd = dev_cmd_make_simple(DEV_CMD_STOP_WASH);
    dev_cmd_receipt_t receipt;

    TEST_ASSERT_NULL(device_command_port_get_ops());
    device_command_port_register(&s_command_ops);
    TEST_ASSERT_EQUAL_PTR(&s_command_ops, device_command_port_get_ops());
    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit(&cmd, &receipt, 0U));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_cloud_report_register_and_get);
    RUN_TEST(test_cloud_link_register_and_get);
    RUN_TEST(test_cloud_property_register_and_get);
    RUN_TEST(test_device_command_port_register_and_get);

    return UNITY_END();
}
