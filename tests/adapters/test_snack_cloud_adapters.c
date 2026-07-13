/**
 * @file    test_snack_cloud_adapters.c
 * @brief   Snack cloud MQTT adapters 单元测试。
 */

#include "adapters/inbound/cloud/providers/snack/snack_cloud_command_adapter.h"
#include "adapters/outbound/cloud/providers/snack/snack_cloud_link_adapter.h"
#include "adapters/outbound/cloud/providers/snack/snack_cloud_report_adapter.h"
#include "common/point_table/point_table.h"
#include "ports/inbound/cloud/property/property_port.h"
#include "ports/outbound/cloud/link/cloud_link_port.h"
#include "ports/outbound/cloud/report/report_port.h"
#include "ports/outbound/storage/deploy_store.h"
#include "tests/stubs/snack_cloud/cloud_model_fake.h"
#include "tests/stubs/snack_cloud/snack_mqtt_fake.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *key;
    const char *value;
} kv_t;

static kv_t                 s_deploy[8];
static unsigned             s_deploy_count;
static char                 s_last_property_json[256];
static point_apply_result_t s_reply_result;
static unsigned             s_property_set_count;
static unsigned             s_reply_count;

static sw_err_t deploy_load(void)
{
    return SW_OK;
}

static sw_err_t deploy_get(const char *key, char *buf, size_t buf_size)
{
    unsigned i;

    if ((key == NULL) || (buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }

    for (i = 0; i < s_deploy_count; i++) {
        if (strcmp(key, s_deploy[i].key) == 0) {
            snprintf(buf, buf_size, "%s", s_deploy[i].value);
            return SW_OK;
        }
    }
    return SW_ERR_PARAM;
}

static const deploy_store_ops_t s_deploy_ops = {
    .load = deploy_load,
    .get  = deploy_get,
};

static void deploy_put(const char *key, const char *value)
{
    TEST_ASSERT_LESS_THAN(sizeof(s_deploy) / sizeof(s_deploy[0]), s_deploy_count);
    s_deploy[s_deploy_count].key   = key;
    s_deploy[s_deploy_count].value = value;
    s_deploy_count++;
}

static sw_err_t property_set_cb(const char *json_payload, point_apply_result_t *result)
{
    snprintf(s_last_property_json, sizeof(s_last_property_json), "%s", json_payload != NULL ? json_payload : "");
    s_property_set_count++;
    if (result != NULL) {
        result->applied  = 2;
        result->rejected = 1;
    }
    return SW_OK;
}

static sw_err_t property_reply_cb(const char *request_json, const point_apply_result_t *result)
{
    (void)request_json;
    s_reply_count++;
    if (result != NULL) {
        s_reply_result = *result;
    }
    return SW_OK;
}

static const cloud_property_ops_t s_property_ops = {
    .on_property_set    = property_set_cb,
    .reply_property_set = property_reply_cb,
};

static void register_deploy_defaults(void)
{
    deploy_put("productKey", "pk1");
    deploy_put("deviceName", "dev1");
    deploy_put("deviceSecret", "sec1");
    deploy_put("topicPropertyUp", "/up");
    deploy_put("topicPropertyReply", "/reply");
}

void setUp(void)
{
    memset(s_deploy, 0, sizeof(s_deploy));
    s_deploy_count = 0;
    memset(s_last_property_json, 0, sizeof(s_last_property_json));
    memset(&s_reply_result, 0, sizeof(s_reply_result));
    s_property_set_count = 0;
    s_reply_count        = 0;

    snack_mqtt_fake_reset();
    snack_mqtt_fake_set_online(1);
    snack_cloud_model_fake_reset();
    register_deploy_defaults();
    deploy_store_register(&s_deploy_ops);
}

void tearDown(void)
{
}

static void test_link_init_loads_credentials_and_publish_uses_mqtt(void)
{
    const cloud_link_ops_t *link;

    snack_cloud_link_adapter_register();
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

    snack_cloud_link_adapter_register();
    link = cloud_link_get_ops();
    snack_mqtt_fake_set_online(0);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, link->publish("/topic", "{}"));
}

static void test_report_adapter_publishes_full_and_delta_json(void)
{
    const cloud_report_ops_t *report;
    const char               *ids[] = {"speed", "state"};

    snack_cloud_link_adapter_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_link_get_ops()->init());
    snack_cloud_report_adapter_register();
    report = cloud_report_get_ops();
    TEST_ASSERT_NOT_NULL(report);

    TEST_ASSERT_EQUAL_INT(SW_OK, report->publish_properties());
    TEST_ASSERT_EQUAL_STRING("/up", snack_mqtt_fake_last_topic());
    TEST_ASSERT_EQUAL_STRING("{\"full\":true}", snack_mqtt_fake_last_payload());

    TEST_ASSERT_EQUAL_INT(SW_OK, report->publish_properties_delta(ids, 2U));
    TEST_ASSERT_EQUAL_STRING("{\"delta\":true}", snack_mqtt_fake_last_payload());
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, report->publish_properties_delta(NULL, 1U));
}

static void test_report_adapter_handles_builder_failure_and_offline_link(void)
{
    const cloud_report_ops_t *report;

    snack_cloud_link_adapter_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_link_get_ops()->init());
    snack_cloud_report_adapter_register();
    report = cloud_report_get_ops();

    snack_cloud_model_fake_set_full_result(SW_ERR_PARAM);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, report->publish_properties());

    snack_cloud_model_fake_set_full_result(SW_OK);
    snack_mqtt_fake_set_online(0);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, report->publish_properties());
}

static void test_command_adapter_dispatches_inbound_property_and_reply(void)
{
    mqtt_recv_handler_t cb;

    snack_cloud_link_adapter_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_cloud_command_adapter_register());
    cloud_property_register(&s_property_ops);
    cb = snack_mqtt_fake_recv_handler();
    TEST_ASSERT_NOT_NULL(cb);

    cb("{\"params\":{\"speed\":1}}");

    TEST_ASSERT_EQUAL_UINT(1U, s_property_set_count);
    TEST_ASSERT_EQUAL_STRING("{\"params\":{\"speed\":1}}", s_last_property_json);
    TEST_ASSERT_EQUAL_UINT(1U, s_reply_count);
    TEST_ASSERT_EQUAL_UINT(2U, s_reply_result.applied);
    TEST_ASSERT_EQUAL_UINT(1U, s_reply_result.rejected);
}

static void test_command_reply_publishes_summary_when_topic_exists(void)
{
    point_apply_result_t result;

    snack_cloud_link_adapter_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_link_get_ops()->init());
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

    RUN_TEST(test_link_init_loads_credentials_and_publish_uses_mqtt);
    RUN_TEST(test_link_offline_publish_returns_comm_error);
    RUN_TEST(test_report_adapter_publishes_full_and_delta_json);
    RUN_TEST(test_report_adapter_handles_builder_failure_and_offline_link);
    RUN_TEST(test_command_adapter_dispatches_inbound_property_and_reply);
    RUN_TEST(test_command_reply_publishes_summary_when_topic_exists);

    return UNITY_END();
}
