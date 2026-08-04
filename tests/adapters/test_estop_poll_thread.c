/**
 * @file    test_estop_poll_thread.c
 * @brief   急停轮询采集线程单元测试
 *
 * @note    thread_registry 无 reset；用例按顺序执行并累加注册数。
 */

#include "adapters/inbound/safety/estop_poll_thread.h"
#include "adapters/outbound/safety/sim/hw_estop_sim.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "ports/outbound/safety/safety_port.h"
#include "runtime/event_bus/event_bus.h"
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

/* 安全端口测试替身：cutout 计数，急停状态取自 hw_estop_sim */
static sw_err_t s_cutout_ret = SW_OK;

static sw_err_t fake_cutout(void)
{
    s_cutout_count++;
    return s_cutout_ret;
}

static bool fake_estop_is_active(void)
{
    return hw_estop_sim_get_active();
}

static bool fake_alarm_is_estop(uint32_t code)
{
    (void)code;
    return false;
}

static void fake_deferred_stop(void)
{
}

static const safety_ops_t s_fake_safety_ops = {
    .cutout          = fake_cutout,
    .estop_is_active = fake_estop_is_active,
    .alarm_is_estop  = fake_alarm_is_estop,
    .deferred_stop   = fake_deferred_stop,
};

void setUp(void)
{
    time_util_init();
    (void)event_bus_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_port_register(&s_fake_safety_ops));
    hw_estop_sim_set_active(false);
    s_estop_on_count  = 0;
    s_estop_off_count = 0;
    s_cutout_count    = 0;
    s_cutout_ret      = SW_OK;
}

void tearDown(void)
{
}

static void test_init_registers_estop_poll_thread(void)
{
    int                   before = thread_registry_count();
    const thread_entry_t *entry;
    sw_err_t              ret;

    ret = estop_poll_thread_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_EQUAL_INT(before + 1, thread_registry_count());

    entry = thread_registry_get(before);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("estop_poll", entry->name);
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

/*
 * 切断失败时事件仍须照常发布。
 *
 * 这是本轮改动要锁住的关键性质：若 cutout 失败让 estop 线程提前返回，领域层
 * 就收不到 EVT_HW_ESTOP_ON，设备既没切断也不进急停态，两条路径同时丢失。
 * 复用上一用例已启动的线程（线程 detach 后无法停止，故不重复启动）。
 */
static void test_cutout_failure_still_publishes_event(void)
{
    /* setUp 的 event_bus_init 会清空订阅表，故本用例自行订阅，不依赖前序用例 */
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_HW_ESTOP_ON, on_estop_on));

    s_cutout_ret     = SW_ERR_HW;
    s_estop_on_count = 0;
    s_cutout_count   = 0;

    hw_estop_sim_set_active(true);
    usleep(30000U);

    TEST_ASSERT_TRUE(s_cutout_count >= 1);
    TEST_ASSERT_TRUE(s_estop_on_count >= 1);

    s_cutout_ret = SW_OK;
    hw_estop_sim_set_active(false);
    usleep(30000U);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_registers_estop_poll_thread);
    RUN_TEST(test_estop_edges_publish_events);
    RUN_TEST(test_cutout_failure_still_publishes_event);
    return UNITY_END();
}
