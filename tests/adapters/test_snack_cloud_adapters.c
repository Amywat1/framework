/**
 * @file    test_snack_cloud_adapters.c
 * @brief   Snack cloud MQTT adapter 单元测试。
 */

#include "adapters/outbound/cloud/providers/snack/snack_cloud_adapter.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "common/point_table/point_table.h"
#include "tests/stubs/snack_cloud/cloud_model_fake.h"
#include "tests/stubs/snack_cloud/snack_mqtt_fake.h"
#include "wdf_test_spec.h"

#include <stdio.h>
#include <string.h>

static char     s_last_recv[256];
static unsigned s_recv_count;

static void test_recv_cb(const char *msg)
{
    snprintf(s_last_recv, sizeof(s_last_recv), "%s", msg != NULL ? msg : "");
    s_recv_count++;
}

static void configure_cloud_defaults(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_cloud_adapter_configure("pk1", "dev1", "sec1", "/up", "/reply"));
}

void setUp(void)
{
    memset(s_last_recv, 0, sizeof(s_last_recv));
    s_recv_count = 0U;
    snack_mqtt_fake_reset();
    snack_mqtt_fake_set_online(1);
    snack_cloud_model_fake_reset();
}

void tearDown(void)
{
}

static void test_link_init_loads_credentials_and_publish_uses_mqtt(void)
{
    const cloud_link_ops_t *link;

    snack_cloud_adapter_register();
    configure_cloud_defaults();
    link = cloud_link_get_ops();
    TEST_ASSERT_NOT_NULL(link);
    TEST_ASSERT_EQUAL_INT(SW_OK, link->init());
    TEST_ASSERT_TRUE(link->is_online());
    TEST_ASSERT_EQUAL_STRING("pk1", snack_mqtt_fake_product_key());
    TEST_ASSERT_EQUAL_STRING("dev1", snack_mqtt_fake_device_name());
    TEST_ASSERT_EQUAL_STRING("sec1", snack_mqtt_fake_device_secret());

    TEST_ASSERT_EQUAL_INT(SW_OK, link->publish("/topic", "{\"ok\":1}"));
    TEST_ASSERT_EQUAL_STRING("/topic", snack_mqtt_fake_last_topic());
    TEST_ASSERT_EQUAL_STRING("{\"ok\":1}", snack_mqtt_fake_last_payload());

    snack_mqtt_fake_set_send_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, link->publish("/topic", "{}"));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, link->publish(NULL, "{}"));
}

static void test_link_offline_publish_returns_comm_error(void)
{
    const cloud_link_ops_t *link;

    snack_cloud_adapter_register();
    configure_cloud_defaults();
    link = cloud_link_get_ops();
    snack_mqtt_fake_set_online(0);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, link->publish("/topic", "{}"));
}

static void test_adapter_publishes_full_and_delta_json(void)
{
    const cloud_link_ops_t *link;
    const char             *ids[] = {"speed", "state"};

    snack_cloud_adapter_register();
    configure_cloud_defaults();
    link = cloud_link_get_ops();
    TEST_ASSERT_EQUAL_INT(SW_OK, link->init());

    TEST_ASSERT_EQUAL_INT(SW_OK, link->publish_properties());
    TEST_ASSERT_EQUAL_STRING("/up", snack_mqtt_fake_last_topic());
    TEST_ASSERT_EQUAL_STRING("{\"full\":true}", snack_mqtt_fake_last_payload());

    TEST_ASSERT_EQUAL_INT(SW_OK, link->publish_properties_delta(ids, 2U));
    TEST_ASSERT_EQUAL_STRING("{\"delta\":true}", snack_mqtt_fake_last_payload());
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, link->publish_properties_delta(NULL, 1U));
}

static void test_adapter_handles_builder_failure_and_offline_link(void)
{
    const cloud_link_ops_t *link;

    snack_cloud_adapter_register();
    configure_cloud_defaults();
    link = cloud_link_get_ops();
    TEST_ASSERT_EQUAL_INT(SW_OK, link->init());

    snack_cloud_model_fake_set_full_result(SW_ERR_PARAM);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, link->publish_properties());

    snack_cloud_model_fake_set_full_result(SW_OK);
    snack_mqtt_fake_set_online(0);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, link->publish_properties());
}

static void test_recv_handler_and_property_reply(void)
{
    const cloud_link_ops_t *link;
    mqtt_recv_handler_t     cb;
    point_apply_result_t    result;

    snack_cloud_adapter_register();
    configure_cloud_defaults();
    link = cloud_link_get_ops();
    TEST_ASSERT_EQUAL_INT(SW_OK, link->init());
    link->set_recv_handler(test_recv_cb);

    cb = snack_mqtt_fake_recv_handler();
    TEST_ASSERT_NOT_NULL(cb);
    cb("{\"enabled\":true}");
    TEST_ASSERT_EQUAL_UINT(1U, s_recv_count);
    TEST_ASSERT_EQUAL_STRING("{\"enabled\":true}", s_last_recv);

    point_apply_result_init(&result);
    result.applied  = 3;
    result.rejected = 1;
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_cloud_property_reply("{}", &result));
    TEST_ASSERT_EQUAL_STRING("/reply", snack_mqtt_fake_last_topic());
    TEST_ASSERT_EQUAL_STRING("{\"applied\":3,\"rejected\":1}", snack_mqtt_fake_last_payload());
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_link_init_loads_credentials_and_publish_uses_mqtt, "", "验证链路初始化加载凭据且通过 MQTT 发布");
    WDF_RUN_TEST(test_link_offline_publish_returns_comm_error, "", "验证链路离线发布返回通信错误");
    WDF_RUN_TEST(test_adapter_publishes_full_and_delta_json, "", "验证适配器发布全量和增量 JSON");
    WDF_RUN_TEST(test_adapter_handles_builder_failure_and_offline_link, "", "验证适配器处理构建失败和链路离线");
    WDF_RUN_TEST(test_recv_handler_and_property_reply, "", "验证接收回调与属性应答");

    return UNITY_END();
}
