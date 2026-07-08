/**
 * @file    test_cloud_point_watcher.c
 * @brief   cloud_point_watcher 与 get 失败策略单元测试
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "framework/cloud/cloud_point.h"
#include "framework/cloud/cloud_point_watcher.h"
#include "framework/common/point_table/point_table.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "third_party/cJSON/cJSON.h"
#include "unity.h"

#include <string.h>

static int32_t s_mock_pos = 0;
static int     s_dirty_events;

static sw_err_t mock_get_pos(point_value_t *out)
{
    out->i = s_mock_pos;
    return SW_OK;
}

static sw_err_t mock_get_fail(point_value_t *out)
{
    (void)out;
    return SW_ERR_PARAM;
}

static const cloud_point_entry_t s_test_model[] = {
    {
        { "sts_gantry_position", POINT_TYPE_INT, mock_get_pos, NULL },
        CLOUD_POINT_ACCESS_RO,
        CLOUD_POINT_SEM_TELEMETRY,
        CLOUD_REPORT_ON_CHANGE,
        CMD_NONE,
        NULL,
    },
    {
        { "sts_bad", POINT_TYPE_INT, mock_get_fail, NULL },
        CLOUD_POINT_ACCESS_RO,
        CLOUD_POINT_SEM_TELEMETRY,
        CLOUD_REPORT_PERIODIC,
        CMD_NONE,
        NULL,
    },
};

static void on_point_dirty(const event_t *evt)
{
    if ((evt != NULL) && (evt->type == EVT_CLOUD_POINT_DIRTY))
    {
        s_dirty_events++;
    }
}

void setUp(void)
{
    s_mock_pos      = 100;
    s_dirty_events  = 0;
    cloud_point_set_get_fail_policy(CLOUD_POINT_GET_FAIL_OMIT);
    (void)event_bus_init();
    (void)event_subscribe(EVT_CLOUD_POINT_DIRTY, on_point_dirty);
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_watcher_init(s_test_model, 2U));
}

void tearDown(void) {}

static void test_watcher_publishes_on_change(void)
{
    cloud_point_watcher_poll();
    TEST_ASSERT_EQUAL_INT(0, s_dirty_events);

    s_mock_pos = 200;
    cloud_point_watcher_poll();

    event_bus_dispatch_loop();
    TEST_ASSERT_EQUAL_INT(1, s_dirty_events);
}

static void test_get_fail_null_policy(void)
{
    static const point_table_entry_t entries[] = {
        { "sts_bad", POINT_TYPE_INT, mock_get_fail, NULL },
    };
    char buf[64];

    cloud_point_set_get_fail_policy(CLOUD_POINT_GET_FAIL_NULL);
    TEST_ASSERT_EQUAL_INT(SW_OK,
                          point_table_to_json_ex(entries, 1U, buf, sizeof(buf),
                                                  POINT_GET_FAIL_NULL, NULL));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"sts_bad\":null"));
}

static sw_err_t mock_get_float(point_value_t *out)
{
    out->f = 36.5f;
    return SW_OK;
}

static void test_float_type_roundtrip(void)
{
    static const point_table_entry_t entries[] = {
        {
            "sts_temp",
            POINT_TYPE_FLOAT,
            mock_get_float,
            NULL,
        },
    };
    char     buf[64];
    cJSON   *root;

    TEST_ASSERT_EQUAL_INT(SW_OK, point_table_to_json(entries, 1U, buf, sizeof(buf)));
    root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL_DOUBLE(36.5, cJSON_GetObjectItem(root, "sts_temp")->valuedouble);
    cJSON_Delete(root);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_watcher_publishes_on_change);
    RUN_TEST(test_get_fail_null_policy);
    RUN_TEST(test_float_type_roundtrip);
    return UNITY_END();
}
