/**
 * @file    test_cloud_point_watcher.c
 * @brief   cloud_point_watcher on_change 变更检测单元测试
 */

#include "common/sw_error.h"
#include "domain/cloud/cloud_point.h"
#include "domain/cloud/cloud_point_watcher.h"
#include "wdf_test_spec.h"

#include <string.h>

static int32_t s_value;

static sw_err_t get_value(point_value_t *out)
{
    out->i = s_value;
    return SW_OK;
}

static cloud_point_entry_t make_on_change_entry(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id   = "speed";
    entry.base.type = POINT_TYPE_INT;
    entry.base.get  = get_value;
    entry.kind      = CLOUD_KIND_TELEMETRY;
    entry.on_change = true;
    return entry;
}

void setUp(void)
{
    s_value = 10;
    cloud_point_watcher_reset_for_test();
}

void tearDown(void)
{
    cloud_point_watcher_reset_for_test();
}

static void test_poll_without_change_no_dirty(void)
{
    const cloud_point_entry_t entries[] = {make_on_change_entry()};
    const char               *ids[4];

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_watcher_init(entries, 1U));

    cloud_point_watcher_poll();
    TEST_ASSERT_EQUAL_UINT(0U, cloud_point_watcher_take_dirty(ids, 4U));
}

static void test_poll_after_change_marks_dirty(void)
{
    const cloud_point_entry_t entries[] = {make_on_change_entry()};
    const char               *ids[4];

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_watcher_init(entries, 1U));

    s_value = 20;
    cloud_point_watcher_poll();
    TEST_ASSERT_EQUAL_UINT(1U, cloud_point_watcher_take_dirty(ids, 4U));
    TEST_ASSERT_EQUAL_STRING("speed", ids[0]);
    TEST_ASSERT_EQUAL_UINT(0U, cloud_point_watcher_take_dirty(ids, 4U));
}

static void test_restore_dirty_keeps_id(void)
{
    const cloud_point_entry_t entries[] = {make_on_change_entry()};
    const char               *ids[4];
    const char               *restore[1];

    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_watcher_init(entries, 1U));
    s_value = 20;
    cloud_point_watcher_poll();
    TEST_ASSERT_EQUAL_UINT(1U, cloud_point_watcher_take_dirty(ids, 4U));

    restore[0] = ids[0];
    cloud_point_watcher_restore_dirty(restore, 1U);
    TEST_ASSERT_EQUAL_UINT(1U, cloud_point_watcher_take_dirty(ids, 4U));
    TEST_ASSERT_EQUAL_STRING("speed", ids[0]);
}

static void test_init_rejects_invalid_args(void)
{
    const cloud_point_entry_t entries[] = {make_on_change_entry()};

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_watcher_init(NULL, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_watcher_init(entries, 0U));
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_poll_without_change_no_dirty, "", "验证点位无变化时轮询不置脏");
    WDF_RUN_TEST(test_poll_after_change_marks_dirty, "", "验证点位变化后轮询置脏");
    WDF_RUN_TEST(test_restore_dirty_keeps_id, "", "验证上报失败后可恢复脏标记");
    WDF_RUN_TEST(test_init_rejects_invalid_args, "", "验证初始化拒绝无效参数");

    return UNITY_END();
}
