/**
 * @file    test_control_loop.c
 * @brief   控制环登记与统计单元测试
 */

#include "common/sw_error.h"
#include "runtime/config/thread_config.h"
#include "runtime/scheduler/control_loop.h"
#include "runtime/scheduler/thread_registry.h"
#include "wdf_test_spec.h"

#include <stddef.h>

static void control_loop_tick_fn(void *ctx)
{
    (void)ctx;
}

void setUp(void)
{
    thread_registry_reset_for_test();
    control_loop_reset_for_test();
}

void tearDown(void)
{
    control_loop_reset_for_test();
    thread_registry_reset_for_test();
}

static void test_control_loop_register_rejects_invalid_args(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, control_loop_register(NULL, 10U, control_loop_tick_fn, NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, control_loop_register("tick", 0U, control_loop_tick_fn, NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, control_loop_register("tick", 10U, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, control_loop_register("tick", 15U, control_loop_tick_fn, NULL));
    TEST_ASSERT_EQUAL_UINT(0U, control_loop_count());
}

static void test_control_loop_register_and_get_stats(void)
{
    periodic_task_stats_t stats;
    sw_err_t              ret;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, control_loop_get_stats(0U, NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_FOUND, control_loop_get_stats(0U, &stats));

    ret = control_loop_register("motor_tick", 10U, control_loop_tick_fn, NULL);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_EQUAL_UINT(1U, control_loop_count());

    TEST_ASSERT_EQUAL_INT(SW_OK, control_loop_get_stats(0U, &stats));
    TEST_ASSERT_EQUAL_STRING("motor_tick", stats.name);
    TEST_ASSERT_EQUAL_UINT(10U, stats.period_ms);
    TEST_ASSERT_EQUAL_UINT(0U, stats.skip_count);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_FOUND, control_loop_get_stats(1U, &stats));
}

static void test_control_loop_register_also_registers_watchdog(void)
{
    const thread_entry_t *loop;
    const thread_entry_t *wd;

    TEST_ASSERT_EQUAL_INT(0, thread_registry_count());
    TEST_ASSERT_EQUAL_INT(SW_OK, control_loop_register("motor_tick", 10U, control_loop_tick_fn, NULL));
    TEST_ASSERT_EQUAL_INT(2, thread_registry_count());

    loop = thread_registry_get(0);
    wd   = thread_registry_get(1);
    TEST_ASSERT_NOT_NULL(loop);
    TEST_ASSERT_NOT_NULL(wd);
    TEST_ASSERT_EQUAL_STRING("control_loop", loop->name);
    TEST_ASSERT_EQUAL_INT(THD_CONTROL_LOOP_PRIO, loop->prio);
    TEST_ASSERT_EQUAL_STRING("control_loop_wd", wd->name);
    TEST_ASSERT_EQUAL_INT(THD_CONTROL_LOOP_WD_PRIO, wd->prio);

    TEST_ASSERT_EQUAL_INT(SW_OK, control_loop_register("fluid_path_poll", 100U, control_loop_tick_fn, NULL));
    TEST_ASSERT_EQUAL_INT(2, thread_registry_count());
    TEST_ASSERT_EQUAL_UINT(2U, control_loop_count());
}

static void test_control_loop_register_overflow(void)
{
    unsigned i;
    char     names[CONTROL_LOOP_SLOT_MAX][8];

    for (i = 0U; i < CONTROL_LOOP_SLOT_MAX; i++) {
        names[i][0] = (char)('a' + (int)i);
        names[i][1] = '\0';
        TEST_ASSERT_EQUAL_INT(SW_OK, control_loop_register(names[i], 10U, control_loop_tick_fn, NULL));
    }
    TEST_ASSERT_EQUAL_UINT(CONTROL_LOOP_SLOT_MAX, control_loop_count());
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, control_loop_register("full", 10U, control_loop_tick_fn, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_control_loop_register_rejects_invalid_args, "", "验证控制环拒绝非法周期与空指针");
    WDF_RUN_TEST(test_control_loop_register_and_get_stats, "", "验证控制环登记后可读统计");
    WDF_RUN_TEST(test_control_loop_register_also_registers_watchdog, "", "验证首次登记同时挂上控制环看门狗线程");
    WDF_RUN_TEST(test_control_loop_register_overflow, "", "验证控制环槽位满时返回溢出");
    return UNITY_END();
}
