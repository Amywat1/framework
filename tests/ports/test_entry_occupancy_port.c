/**
 * @file    test_entry_occupancy_port.c
 * @brief   入场占用口注册与未注册查询
 */

#include "common/sw_error.h"
#include "domain/ports/outbound/device/entry_occupancy_port.h"
#include "wdf_test_spec.h"

static entry_occupancy_phase_t s_phase;

static entry_occupancy_t stub_get(void)
{
    entry_occupancy_t view;

    view.available = false;
    view.phase     = s_phase;
    return view;
}

static entry_occupancy_t stub_get_ready(void)
{
    entry_occupancy_t view;

    view.available = false;
    view.phase     = ENTRY_OCC_READY;
    return view;
}

static const entry_occupancy_ops_t s_ops = {
    .get = stub_get,
};

static const entry_occupancy_ops_t s_ops_ready = {
    .get = stub_get_ready,
};

static const entry_occupancy_ops_t s_ops_missing = {
    .get = NULL,
};

void setUp(void)
{
    s_phase = ENTRY_OCC_OCCUPIED;
    TEST_ASSERT_EQUAL_INT(SW_OK, entry_occupancy_register(NULL));
}

void tearDown(void)
{
    (void)entry_occupancy_register(NULL);
}

static void test_unregistered_is_unavailable_empty(void)
{
    entry_occupancy_t view = entry_occupancy_get();

    TEST_ASSERT_NULL(entry_occupancy_get_ops());
    TEST_ASSERT_FALSE(view.available);
    TEST_ASSERT_EQUAL_INT(ENTRY_OCC_EMPTY, view.phase);
    view = entry_occupancy_get();
    TEST_ASSERT_FALSE(view.available);
}

static void test_register_returns_project_phase(void)
{
    entry_occupancy_t view;

    TEST_ASSERT_EQUAL_INT(SW_OK, entry_occupancy_register(&s_ops));
    TEST_ASSERT_EQUAL_PTR(&s_ops, entry_occupancy_get_ops());
    view = entry_occupancy_get();
    TEST_ASSERT_TRUE(view.available);
    TEST_ASSERT_EQUAL_INT(ENTRY_OCC_OCCUPIED, view.phase);
}

static void test_null_get_rejected_keeps_previous(void)
{
    entry_occupancy_t view;

    TEST_ASSERT_EQUAL_INT(SW_OK, entry_occupancy_register(&s_ops));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, entry_occupancy_register(&s_ops_missing));
    TEST_ASSERT_EQUAL_PTR(&s_ops, entry_occupancy_get_ops());
    view = entry_occupancy_get();
    TEST_ASSERT_TRUE(view.available);
    TEST_ASSERT_EQUAL_INT(ENTRY_OCC_OCCUPIED, view.phase);
}

static void test_reregister_replaces(void)
{
    entry_occupancy_t view;

    TEST_ASSERT_EQUAL_INT(SW_OK, entry_occupancy_register(&s_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, entry_occupancy_register(&s_ops_ready));
    view = entry_occupancy_get();
    TEST_ASSERT_TRUE(view.available);
    TEST_ASSERT_EQUAL_INT(ENTRY_OCC_READY, view.phase);
}

static void test_unregister_restores_unavailable(void)
{
    entry_occupancy_t view;

    TEST_ASSERT_EQUAL_INT(SW_OK, entry_occupancy_register(&s_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, entry_occupancy_register(NULL));
    view = entry_occupancy_get();
    TEST_ASSERT_FALSE(view.available);
    TEST_ASSERT_EQUAL_INT(ENTRY_OCC_EMPTY, view.phase);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_unregistered_is_unavailable_empty, "PORT-07",
                 "未注册占用口 available 为假且相位 EMPTY");
    WDF_RUN_TEST(test_register_returns_project_phase, "PORT-01",
                 "注册后返回项目相位并置 available");
    WDF_RUN_TEST(test_null_get_rejected_keeps_previous, "PORT-02",
                 "缺 get 拒绝且保持原注册");
    WDF_RUN_TEST(test_reregister_replaces, "PORT-03",
                 "重复注册以最后一次为准");
    WDF_RUN_TEST(test_unregister_restores_unavailable, "PORT-01",
                 "解除注册后回到未注册查询");
    return UNITY_END();
}
