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
#include "domain/ports/outbound/safety/safety_port.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/scheduler.h"
#include "runtime/scheduler/thread_registry.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

static volatile int s_estop_on_count;
static volatile int s_estop_off_count;
static volatile int s_cutout_count;
static volatile int s_slow_handler_started;
static volatile int s_slow_handler_finished;

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

/* 占用 dispatch 的慢 handler：验证 cutout 仍在 estop_poll 线程立即执行 */
static void on_slow_dispatch_work(const event_t *evt)
{
    (void)evt;
    s_slow_handler_started = 1;
    usleep(200000U);
    s_slow_handler_finished = 1;
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

    ret = estop_poll_thread_init(NULL);
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

/*
 * cutout 在 estop_poll 线程执行，不得被 dispatch 上的慢 handler 拖住。
 * 复用已启动的 estop_poll / event_dispatch 线程。
 */
static void test_cutout_runs_while_dispatch_handler_blocked(void)
{
    int cutout_before;
    int spins;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_CONTEXT_SYNC, on_slow_dispatch_work));

    s_slow_handler_started  = 0;
    s_slow_handler_finished = 0;
    cutout_before           = s_cutout_count;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_CONTEXT_SYNC, 0U));

    for (spins = 0; (s_slow_handler_started == 0) && (spins < 100); spins++) {
        usleep(1000U);
    }
    TEST_ASSERT_EQUAL_INT(1, s_slow_handler_started);
    TEST_ASSERT_EQUAL_INT(0, s_slow_handler_finished);

    hw_estop_sim_set_active(true);
    for (spins = 0; ((s_cutout_count - cutout_before) < 1) && (spins < 40); spins++) {
        usleep(1000U);
    }

    TEST_ASSERT_TRUE((s_cutout_count - cutout_before) >= 1);
    TEST_ASSERT_EQUAL_INT(0, s_slow_handler_finished);

    for (spins = 0; (s_slow_handler_finished == 0) && (spins < 300); spins++) {
        usleep(1000U);
    }
    TEST_ASSERT_EQUAL_INT(1, s_slow_handler_finished);

    hw_estop_sim_set_active(false);
    usleep(30000U);
}

static void test_filter_immediate_on_zero_confirm(void)
{
    estop_filter_t   filter;
    estop_poll_cfg_t cfg = {.confirm_on_ms = 0U, .confirm_off_ms = 0U};

    estop_filter_reset(&filter, &cfg);
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_CONFIRMED_ON, estop_filter_feed(&filter, true, 0U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, true, 5U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_CONFIRMED_OFF, estop_filter_feed(&filter, false, 5U));
}

static void test_filter_startup_inactive_does_not_emit_off(void)
{
    estop_filter_t   filter;
    estop_poll_cfg_t cfg = {.confirm_on_ms = 0U, .confirm_off_ms = 0U};

    estop_filter_reset(&filter, &cfg);
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, false, 0U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_CONFIRMED_ON, estop_filter_feed(&filter, true, 0U));
}

static void test_filter_rejects_on_glitch(void)
{
    estop_filter_t   filter;
    estop_poll_cfg_t cfg = {.confirm_on_ms = 60U, .confirm_off_ms = 90U};

    estop_filter_reset(&filter, &cfg);
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, true, 0U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, true, 50U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, false, 51U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, false, 200U));
}

static void test_filter_confirms_stable_on_and_off(void)
{
    estop_filter_t   filter;
    estop_poll_cfg_t cfg = {.confirm_on_ms = 60U, .confirm_off_ms = 90U};

    estop_filter_reset(&filter, &cfg);
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, true, 0U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_CONFIRMED_ON, estop_filter_feed(&filter, true, 60U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, false, 70U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, false, 150U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_CONFIRMED_OFF, estop_filter_feed(&filter, false, 160U));
}

static void test_filter_startup_pressed_confirms_on(void)
{
    estop_filter_t   filter;
    estop_poll_cfg_t cfg = {.confirm_on_ms = 60U, .confirm_off_ms = 90U};

    estop_filter_reset(&filter, &cfg);
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, true, 10U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_HOLD, estop_filter_feed(&filter, true, 69U));
    TEST_ASSERT_EQUAL_INT(ESTOP_FILTER_CONFIRMED_ON, estop_filter_feed(&filter, true, 70U));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_filter_immediate_on_zero_confirm, "", "验证零确认时间立即输出边沿");
    WDF_RUN_TEST(test_filter_startup_inactive_does_not_emit_off, "", "验证上电无效不发松开事件");
    WDF_RUN_TEST(test_filter_rejects_on_glitch, "", "验证短于确认窗的毛刺不触发");
    WDF_RUN_TEST(test_filter_confirms_stable_on_and_off, "", "验证稳定按下与松开完成确认");
    WDF_RUN_TEST(test_filter_startup_pressed_confirms_on, "", "验证上电已按下经确认后触发");
    WDF_RUN_TEST(test_init_registers_estop_poll_thread, "", "验证初始化注册急停轮询线程");
    WDF_RUN_TEST(test_estop_edges_publish_events, "", "验证急停边沿发布事件");
    WDF_RUN_TEST(test_cutout_failure_still_publishes_event, "", "验证安全切断失败仍然发布事件");
    WDF_RUN_TEST(test_cutout_runs_while_dispatch_handler_blocked, "", "验证dispatch阻塞时急停仍立即切断");
    return UNITY_END();
}
