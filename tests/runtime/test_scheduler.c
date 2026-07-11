/**
 * @file    test_scheduler.c
 * @brief   scheduler / thread_registry / periodic_task 单元测试
 *
 * @note    thread_registry 无 reset 接口，用例按注册顺序执行并累加计数。
 */

#include "common/sw_error.h"
#include "runtime/scheduler/periodic_task.h"
#include "runtime/scheduler/scheduler.h"
#include "runtime/scheduler/thread_registry.h"
#include "unity.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile int s_one_shot_done;
static volatile int s_periodic_tick_count;

static void *one_shot_thread_fn(void *arg)
{
    (void)arg;
    s_one_shot_done = 1;
    return NULL;
}

static void periodic_tick_fn(void *ctx)
{
    (void)ctx;
    s_periodic_tick_count++;
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_registry_count_initial_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, thread_registry_count());
}

static void test_register_rejects_null_name(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
                          thread_register(NULL, one_shot_thread_fn, SCHED_OTHER, 0, 4096U));
    TEST_ASSERT_EQUAL_INT(0, thread_registry_count());
}

static void test_register_rejects_null_fn(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
                          thread_register("bad", NULL, SCHED_OTHER, 0, 4096U));
    TEST_ASSERT_EQUAL_INT(0, thread_registry_count());
}

static void test_register_and_get_entry(void)
{
    sw_err_t ret;

    ret = thread_register("worker", one_shot_thread_fn, SCHED_OTHER, 0, 8192U);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_EQUAL_INT(1, thread_registry_count());

    {
        const thread_entry_t *entry = thread_registry_get(0);
        TEST_ASSERT_NOT_NULL(entry);
        TEST_ASSERT_EQUAL_STRING("worker", entry->name);
        TEST_ASSERT_EQUAL_PTR(one_shot_thread_fn, entry->fn);
        TEST_ASSERT_NULL(entry->arg);
        TEST_ASSERT_EQUAL_INT(SCHED_OTHER, entry->sched_policy);
        TEST_ASSERT_EQUAL_INT(0, entry->prio);
        TEST_ASSERT_EQUAL_UINT32(8192U, (unsigned)entry->stack_size);
    }
}

static void test_register_arg_passes_context(void)
{
    static int ctx_value = 42;
    sw_err_t   ret;

    ret = thread_register_arg("ctx_worker",
                              one_shot_thread_fn,
                              &ctx_value,
                              SCHED_OTHER,
                              0,
                              4096U);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_EQUAL_INT(2, thread_registry_count());
    TEST_ASSERT_EQUAL_PTR(&ctx_value, thread_registry_get(1)->arg);
}

static void test_registry_get_out_of_range(void)
{
    TEST_ASSERT_NULL(thread_registry_get(-1));
    TEST_ASSERT_NULL(thread_registry_get(thread_registry_count()));
}

static void test_periodic_task_register_rejects_invalid_args(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
                          periodic_task_register(NULL,
                                                 10U,
                                                 periodic_tick_fn,
                                                 NULL,
                                                 SCHED_OTHER,
                                                 0,
                                                 4096U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
                          periodic_task_register("tick",
                                                 0U,
                                                 periodic_tick_fn,
                                                 NULL,
                                                 SCHED_OTHER,
                                                 0,
                                                 4096U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
                          periodic_task_register("tick",
                                                 10U,
                                                 NULL,
                                                 NULL,
                                                 SCHED_OTHER,
                                                 0,
                                                 4096U));
    TEST_ASSERT_EQUAL_INT(2, thread_registry_count());
}

static void test_periodic_task_register_and_scheduler_start(void)
{
    sw_err_t ret;
    int      before_count;

    before_count = thread_registry_count();
    ret          = periodic_task_register("tick",
                                 10U,
                                 periodic_tick_fn,
                                 NULL,
                                 SCHED_OTHER,
                                 0,
                                 8192U);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_EQUAL_INT(before_count + 1, thread_registry_count());

    s_one_shot_done       = 0;
    s_periodic_tick_count = 0;

    ret = scheduler_start_all();
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);

    usleep(80000U);

    TEST_ASSERT_EQUAL_INT(1, s_one_shot_done);
    TEST_ASSERT_TRUE(s_periodic_tick_count >= 2);
}

static void test_registry_overflow(void)
{
    int i;
    int base = thread_registry_count();

    for (i = base; i < THREAD_REGISTRY_MAX; i++) {
        TEST_ASSERT_EQUAL_INT(SW_OK,
                              thread_register("fill", one_shot_thread_fn, SCHED_OTHER, 0, 4096U));
    }

    TEST_ASSERT_EQUAL_INT(THREAD_REGISTRY_MAX, thread_registry_count());
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW,
                          thread_register("overflow",
                                          one_shot_thread_fn,
                                          SCHED_OTHER,
                                          0,
                                          4096U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_registry_count_initial_zero);
    RUN_TEST(test_register_rejects_null_name);
    RUN_TEST(test_register_rejects_null_fn);
    RUN_TEST(test_register_and_get_entry);
    RUN_TEST(test_register_arg_passes_context);
    RUN_TEST(test_registry_get_out_of_range);
    RUN_TEST(test_periodic_task_register_rejects_invalid_args);
    RUN_TEST(test_periodic_task_register_and_scheduler_start);
    RUN_TEST(test_registry_overflow);
    return UNITY_END();
}
