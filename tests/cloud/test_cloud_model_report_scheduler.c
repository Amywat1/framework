/**
 * @file    test_cloud_model_report_scheduler.c
 * @brief   cloud_model 与 report_scheduler 单元测试
 */

#include "adapters/outbound/cloud/cloud_json.h"
#include "application/orchestrators/report_scheduler.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "domain/cloud/cloud_model.h"
#include "domain/cloud/cloud_point.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

#include <stdio.h>
#include <string.h>

static int32_t  s_counter;
static bool     s_enabled;
static bool     s_online;
static unsigned s_full_reports;
static unsigned s_delta_reports;
static char     s_last_delta_id[32];

static sw_err_t get_counter(point_value_t *out)
{
    out->i = s_counter;
    return SW_OK;
}

static sw_err_t get_enabled(point_value_t *out)
{
    out->b = s_enabled;
    return SW_OK;
}

static sw_err_t set_enabled(const point_value_t *in)
{
    s_enabled = in->b;
    return SW_OK;
}

static bool stub_is_online(void)
{
    return s_online;
}

static sw_err_t stub_publish_properties(void)
{
    s_full_reports++;
    return SW_OK;
}

static sw_err_t stub_publish_delta(const char *const *ids, size_t count)
{
    s_delta_reports++;
    if ((ids != NULL) && (count > 0U) && (ids[0] != NULL)) {
        (void)snprintf(s_last_delta_id, sizeof(s_last_delta_id), "%s", ids[0]);
    }
    return SW_OK;
}

static const cloud_link_ops_t s_link_ops = {
    .is_online                = stub_is_online,
    .publish_properties       = stub_publish_properties,
    .publish_properties_delta = stub_publish_delta,
};

static cloud_point_entry_t make_counter(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "counter";
    entry.base.type = POINT_TYPE_INT;
    entry.base.get  = get_counter;
    entry.kind      = CLOUD_KIND_TELEMETRY;
    entry.on_change = true;
    return entry;
}

static cloud_point_entry_t make_enabled(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "enabled";
    entry.base.type = POINT_TYPE_BOOL;
    entry.base.get  = get_enabled;
    entry.base.set  = set_enabled;
    entry.kind      = CLOUD_KIND_WRITE;
    return entry;
}

static cloud_point_entry_t s_entries[2];

static void publish_and_wait(event_type_t type, uint32_t param)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(type, param));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
}

static void register_model(void)
{
    s_entries[0] = make_counter();
    s_entries[1] = make_enabled();
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_model_register(s_entries, 2U));
}

void setUp(void)
{
    s_counter          = 7;
    s_enabled          = false;
    s_online           = true;
    s_full_reports     = 0U;
    s_delta_reports    = 0U;
    s_last_delta_id[0] = '\0';
    cloud_model_reset_for_test();
    report_scheduler_reset_for_test();
}

void tearDown(void)
{
    cloud_model_reset_for_test();
    report_scheduler_reset_for_test();
}

static void test_cloud_model_builds_and_applies_properties(void)
{
    char                 buf[128];
    const char          *ids[] = {"enabled"};
    point_apply_result_t result;

    register_model();
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_json_build_properties(buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"counter\":7"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"enabled\":false"));

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_json_build_properties_delta(ids, 1U, buf, sizeof(buf)));
    TEST_ASSERT_NULL(strstr(buf, "\"counter\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"enabled\":false"));

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_json_install(NULL, NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_json_apply_property_set("{\"enabled\":true}", &result));
    TEST_ASSERT_EQUAL_UINT(1U, result.applied);
    TEST_ASSERT_TRUE(s_enabled);
}

static void test_report_scheduler_resync_and_dirty_delta(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    cloud_link_register(&s_link_ops);
    register_model();
    TEST_ASSERT_EQUAL_INT(SW_OK, report_scheduler_start(500U, 0U));

    publish_and_wait(EVT_CLOUD_CONNECTED, 0U);
    TEST_ASSERT_EQUAL_UINT(1U, s_full_reports);

    s_counter = 8;
    report_scheduler_poll();
    TEST_ASSERT_EQUAL_UINT(1U, s_delta_reports);
    TEST_ASSERT_EQUAL_STRING("counter", s_last_delta_id);

    s_online = false;
    publish_and_wait(EVT_CLOUD_CONNECTED, 0U);
    TEST_ASSERT_EQUAL_UINT(1U, s_full_reports);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_cloud_model_builds_and_applies_properties, "", "验证云端模型构建并应用属性");
    WDF_RUN_TEST(test_report_scheduler_resync_and_dirty_delta, "", "验证重连全量与脏点增量上报");

    return UNITY_END();
}
