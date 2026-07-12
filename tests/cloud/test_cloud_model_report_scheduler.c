/**
 * @file    test_cloud_model_report_scheduler.c
 * @brief   cloud_model 与 report_scheduler 单元测试
 */

#include "application/orchestrators/report_scheduler.h"
#include "cloud/cloud_model.h"
#include "cloud/cloud_point.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "ports/inbound/cloud/property/property_port.h"
#include "ports/outbound/cloud/link/cloud_link_port.h"
#include "ports/outbound/cloud/report/report_port.h"
#include "runtime/event_bus/event_bus.h"
#include "unity.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

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
    .is_online = stub_is_online,
};

static const cloud_report_ops_t s_report_ops = {
    .publish_properties       = stub_publish_properties,
    .publish_properties_delta = stub_publish_delta,
};

static cloud_point_entry_t make_counter(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id       = "counter";
    entry.base.type     = POINT_TYPE_INT;
    entry.base.get      = get_counter;
    entry.access        = CLOUD_POINT_ACCESS_RO;
    entry.semantic      = CLOUD_POINT_SEM_TELEMETRY;
    entry.report_policy = CLOUD_REPORT_ON_CHANGE;
    return entry;
}

static cloud_point_entry_t make_enabled(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id       = "enabled";
    entry.base.type     = POINT_TYPE_BOOL;
    entry.base.get      = get_enabled;
    entry.base.set      = set_enabled;
    entry.access        = CLOUD_POINT_ACCESS_RW;
    entry.semantic      = CLOUD_POINT_SEM_MANUAL_ACT;
    entry.report_policy = CLOUD_REPORT_PERIODIC;
    return entry;
}

static const char *const s_delta_ids[] = {"enabled"};

static const report_policy_entry_t s_policies[] = {
    {
     .kind     = REPORT_TRIGGER_EVENT,
     .event_id = EVT_CLOUD_CONNECTED,
     .full     = true,
     },
    {
     .kind           = REPORT_TRIGGER_EVENT,
     .event_id       = EVT_ALARM_TRIGGERED,
     .delta_ids      = s_delta_ids,
     .delta_id_count = 1U,
     },
    {
     .kind                           = REPORT_TRIGGER_EVENT,
     .event_id                       = EVT_CLOUD_POINT_DIRTY,
     .use_event_param_as_point_index = true,
     },
};

static cloud_point_entry_t s_entries[2];

static void *dispatch_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

static pthread_t start_dispatch(void)
{
    pthread_t tid;

    pthread_create(&tid, NULL, dispatch_fn, NULL);
    return tid;
}

static void stop_dispatch(pthread_t tid)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_shutdown());
    pthread_join(tid, NULL);
}

static void publish_and_wait(event_type_t type, uint32_t param)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(type, param));
    usleep(50000);
}

static void register_model(void)
{
    cloud_model_bundle_t bundle;

    s_entries[0] = make_counter();
    s_entries[1] = make_enabled();
    bundle       = (cloud_model_bundle_t){
              .entries         = s_entries,
              .count           = 2U,
              .report_policies = s_policies,
              .policy_count    = sizeof(s_policies) / sizeof(s_policies[0]),
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_model_register(&bundle));
}

void setUp(void)
{
    s_counter          = 7;
    s_enabled          = false;
    s_online           = true;
    s_full_reports     = 0U;
    s_delta_reports    = 0U;
    s_last_delta_id[0] = '\0';
}

void tearDown(void)
{
}

static void test_cloud_model_builds_and_applies_properties(void)
{
    char                        buf[128];
    const char                 *ids[] = {"enabled"};
    const cloud_property_ops_t *ops;
    point_apply_result_t        result;

    register_model();
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_model_validate_and_watch());
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_model_build_properties(buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"counter\":7"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"enabled\":false"));

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_model_build_properties_delta(ids, 1U, buf, sizeof(buf)));
    TEST_ASSERT_NULL(strstr(buf, "\"counter\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"enabled\":false"));

    ops = cloud_property_get_ops();
    TEST_ASSERT_NOT_NULL(ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, ops->on_property_set("{\"enabled\":true}", &result));
    TEST_ASSERT_EQUAL_UINT(1U, result.applied);
    TEST_ASSERT_TRUE(s_enabled);
}

static void test_report_scheduler_runs_event_policies(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    cloud_link_register(&s_link_ops);
    cloud_report_register(&s_report_ops);
    register_model();
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_model_validate_and_watch());
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_model_start_scheduler());
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_CLOUD_CONNECTED, 0U);
    TEST_ASSERT_EQUAL_UINT(1U, s_full_reports);

    publish_and_wait(EVT_ALARM_TRIGGERED, 201101U);
    TEST_ASSERT_EQUAL_UINT(1U, s_delta_reports);
    TEST_ASSERT_EQUAL_STRING("enabled", s_last_delta_id);

    publish_and_wait(EVT_CLOUD_POINT_DIRTY, 0U);
    TEST_ASSERT_EQUAL_UINT(2U, s_delta_reports);
    TEST_ASSERT_EQUAL_STRING("counter", s_last_delta_id);

    s_online = false;
    report_scheduler_request_resync();
    TEST_ASSERT_EQUAL_UINT(1U, s_full_reports);

    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_cloud_model_builds_and_applies_properties);
    RUN_TEST(test_report_scheduler_runs_event_policies);

    return UNITY_END();
}
