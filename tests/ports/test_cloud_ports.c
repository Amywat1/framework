/**
 * @file    test_cloud_ports.c
 * @brief   cloud 端口 register/get 单元测试
 */

#include "application/ports/inbound/command/command_port.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "common/sw_error.h"
#include "domain/op_mode/command_types.h"
#include "domain/op_mode/device_command.h"
#include "domain/ports/outbound/storage/deploy_store.h"
#include "domain/ports/outbound/storage/param_store.h"
#include "runtime/ports/port_registry.h"
#include "wdf_test_spec.h"

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

static const cloud_link_ops_t s_link_ops = {
    .is_online                = stub_is_online,
    .publish_properties       = stub_publish_properties,
    .publish_properties_delta = stub_publish_delta,
};

static sw_err_t stub_submit_async(const dev_cmd_t *cmd, uint64_t *request_id)
{
    (void)cmd;
    if (request_id != NULL) {
        *request_id = 1U;
    }
    return SW_OK;
}

static sw_err_t stub_submit_sync(const dev_cmd_t *cmd, dev_cmd_receipt_t *receipt, uint32_t timeout_ms)
{
    (void)cmd;
    (void)timeout_ms;
    if (receipt != NULL) {
        receipt->status = DEV_CMD_STATUS_ACCEPTED;
    }
    return SW_OK;
}

static const device_command_port_ops_t s_command_ops = {
    .submit_async = stub_submit_async,
    .submit_sync  = stub_submit_sync,
};

void setUp(void)
{
    port_registry_cloud_reset();
    port_registry_infra_reset();
}

void tearDown(void)
{
    port_registry_cloud_reset();
    port_registry_infra_reset();
}

static void test_cloud_link_register_and_get(void)
{
    TEST_ASSERT_NULL(cloud_link_get_ops());
    cloud_link_register(&s_link_ops);
    TEST_ASSERT_EQUAL_PTR(&s_link_ops, cloud_link_get_ops());
    TEST_ASSERT_TRUE(cloud_link_get_ops()->is_online());
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_link_get_ops()->publish_properties());
}

static void test_device_command_port_register_and_get(void)
{
    dev_cmd_t         cmd = dev_cmd_make_simple(DEV_CMD_STOP_WASH);
    dev_cmd_receipt_t receipt;

    TEST_ASSERT_NULL(device_command_port_get_ops());
    device_command_port_register(&s_command_ops);
    TEST_ASSERT_EQUAL_PTR(&s_command_ops, device_command_port_get_ops());
    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_sync(&cmd, &receipt, 0U));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
}

static sw_err_t stub_store_load(void)
{
    return SW_OK;
}

static sw_err_t stub_store_save(void)
{
    return SW_OK;
}

static sw_err_t stub_store_get(const char *key, char *buf, size_t buf_size)
{
    (void)key;
    (void)buf;
    (void)buf_size;
    return SW_OK;
}

static sw_err_t stub_store_set(const char *key, const char *val)
{
    (void)key;
    (void)val;
    return SW_OK;
}

static void test_register_returns_ok_for_valid_ops(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_link_register(&s_link_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_register(&s_command_ops));
}

static void test_register_null_unregisters(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_link_register(&s_link_ops));
    TEST_ASSERT_NOT_NULL(cloud_link_get_ops());

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_link_register(NULL));
    TEST_ASSERT_NULL(cloud_link_get_ops());

    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_register(&s_command_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_register(NULL));
    TEST_ASSERT_NULL(device_command_port_get_ops());
}

static void test_register_rejects_missing_mandatory_field(void)
{
    static const device_command_port_ops_t s_empty_cmd = {.submit_async = NULL, .submit_sync = NULL};

    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_register(&s_command_ops));
    TEST_ASSERT_EQUAL_PTR(&s_command_ops, device_command_port_get_ops());

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, device_command_port_register(&s_empty_cmd));
    TEST_ASSERT_EQUAL_PTR(&s_command_ops, device_command_port_get_ops());
}

static void test_param_store_requires_all_four_fields(void)
{
    static const param_store_ops_t s_full = {
        .load = stub_store_load,
        .save = stub_store_save,
        .get  = stub_store_get,
        .set  = stub_store_set,
    };
    static const param_store_ops_t s_no_set = {
        .load = stub_store_load,
        .save = stub_store_save,
        .get  = stub_store_get,
        .set  = NULL,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, param_store_register(&s_full));
    TEST_ASSERT_EQUAL_PTR(&s_full, param_store_get_ops());

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, param_store_register(&s_no_set));
    TEST_ASSERT_EQUAL_PTR(&s_full, param_store_get_ops());
}

static void test_deploy_store_requires_load(void)
{
    static const deploy_store_ops_t s_ok      = {.load = stub_store_load, .get = stub_store_get};
    static const deploy_store_ops_t s_no_load = {.load = NULL, .get = stub_store_get};

    TEST_ASSERT_EQUAL_INT(SW_OK, deploy_store_register(&s_ok));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, deploy_store_register(&s_no_load));
    TEST_ASSERT_EQUAL_PTR(&s_ok, deploy_store_get_ops());
}

static void test_register_replaces_on_duplicate(void)
{
    static const cloud_link_ops_t s_other = {
        .is_online                = stub_is_online,
        .publish_properties       = stub_publish_properties,
        .publish_properties_delta = stub_publish_delta,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_link_register(&s_link_ops));
    TEST_ASSERT_EQUAL_PTR(&s_link_ops, cloud_link_get_ops());

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_link_register(&s_other));
    TEST_ASSERT_EQUAL_PTR(&s_other, cloud_link_get_ops());
}

static void test_reset_clears_all_ports(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_link_register(&s_link_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_register(&s_command_ops));

    port_registry_cloud_reset();
    port_registry_infra_reset();

    TEST_ASSERT_NULL(cloud_link_get_ops());
    TEST_ASSERT_NULL(device_command_port_get_ops());
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_cloud_link_register_and_get, "", "验证云端链路注册并获取");
    WDF_RUN_TEST(test_device_command_port_register_and_get, "", "验证设备命令端口注册并获取");
    WDF_RUN_TEST(test_register_returns_ok_for_valid_ops, "", "验证注册返回成功针对有效操作接口");
    WDF_RUN_TEST(test_register_null_unregisters, "", "验证注册空指针注销");
    WDF_RUN_TEST(test_register_rejects_missing_mandatory_field, "", "验证注册拒绝缺失必填字段");
    WDF_RUN_TEST(test_param_store_requires_all_four_fields, "", "验证参数存储要求全部四个字段");
    WDF_RUN_TEST(test_deploy_store_requires_load, "", "验证部署配置存储要求加载");
    WDF_RUN_TEST(test_register_replaces_on_duplicate, "", "验证注册替换开启重复");
    WDF_RUN_TEST(test_reset_clears_all_ports, "", "验证复位清除全部端口");

    return UNITY_END();
}
