/**
 * @file    test_safety_thread.c
 * @brief   safety_thread 单元测试
 *
 * @note    thread_registry 无 reset；用例按顺序执行并累加注册数。
 */

#include "adapters/outbound/safety/sim/hw_estop_sim.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/platform/safety_thread.h"
#include "runtime/scheduler/scheduler.h"
#include "runtime/scheduler/thread_registry.h"
#include "unity.h"

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

static volatile int s_estop_on_count;
static volatile int s_estop_off_count;
static volatile int s_cutout_count;

static void on_estop_on(const event_t *evt)
{
    (void)evt;
    s_estop_on_count++;
}

static void on_estop_off(const event_t *evt)
{
    (void)evt;
    s_estop_off_count++;
}

static void *event_dispatch_thread_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

void safety_cutout_execute(void)
{
    s_cutout_count++;
}

void setUp(void)
{
    time_util_init();
    (void)event_bus_init();
    hw_estop_sim_set_active(false);
    s_estop_on_count  = 0;
    s_estop_off_count = 0;
    s_cutout_count    = 0;
}

void tearDown(void)
{
}

static void test_init_registers_safety_thread(void)
{
    int                   before = thread_registry_count();
    const thread_entry_t *entry;
    sw_err_t              ret;

    ret = safety_thread_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_EQUAL_INT(before + 1, thread_registry_count());

    entry = thread_registry_get(before);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("safety_thread", entry->name);
    TEST_ASSERT_EQUAL_INT(SCHED_FIFO, entry->sched_policy);
}

static void test_estop_edges_publish_events(void)
{
    sw_err_t ret;

    ret = event_subscribe(EVT_HW_ESTOP_ON, on_estop_on);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    ret = event_subscribe(EVT_HW_ESTOP_OFF, on_estop_off);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);

    ret = thread_register("event_dispatch", event_dispatch_thread_fn, SCHED_OTHER, 0, 16U * 1024U);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);

    ret = scheduler_start_all();
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);

    usleep(15000U);

    hw_estop_sim_set_active(true);
    usleep(30000U);
    TEST_ASSERT_TRUE(s_estop_on_count >= 1);
    TEST_ASSERT_TRUE(s_cutout_count >= 1);

    s_estop_off_count = 0;
    hw_estop_sim_set_active(false);
    usleep(30000U);
    TEST_ASSERT_TRUE(s_estop_off_count >= 1);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_registers_safety_thread);
    RUN_TEST(test_estop_edges_publish_events);
    return UNITY_END();
}
