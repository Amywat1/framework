/**
 * @file    test_event_bus_capacity.c
 * @brief   事件总线容量水位实测
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    目的不是"断言容量够用"，而是把容量常量的依据固定下来：
 *          `EVENT_BUS_QUEUE_SIZE` 等原先是拍定的数，没有实测支撑。本用例用
 *          与真机相当的并发发布密度压出实际水位，并断言留有余量。若将来事件
 *          密度上升到吃掉余量，这里会先失败，而不是等到现场丢事件。
 *
 * @note    慢 handler 是本框架队列积压的主因：dispatch 单线程串行执行所有
 *          handler，一个慢 handler 会让其后所有事件排队。因此除稳态密度外，
 *          还专门测了"handler 阻塞期间持续发布"这一最坏情况。
 */

#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/event_bus/event_bus_config.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_t    s_dispatch_tid;
static volatile int s_handled;
static volatile int s_block_handler;
static volatile int s_handler_entered;

static void *dispatch_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

static void counting_handler(const event_t *evt)
{
    (void)evt;
    s_handled++;
}

/** 可控阻塞的 handler，用于制造队列积压 */
static void blocking_handler(const event_t *evt)
{
    (void)evt;
    s_handler_entered = 1;
    while (s_block_handler) {
        usleep(1000U);
    }
    s_handled++;
}

void setUp(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    s_handled         = 0;
    s_block_handler   = 0;
    s_handler_entered = 0;
}

void tearDown(void)
{
    s_block_handler = 0;
    (void)event_bus_shutdown();
    pthread_join(s_dispatch_tid, NULL);
}

static void start_dispatch(void)
{
    pthread_create(&s_dispatch_tid, NULL, dispatch_fn, NULL);
    usleep(10000U);
}

/* -------------------------------------------------------------------------
 * 稳态密度
 *
 * M8 实测的周期任务构成（周期越短发布越密）：
 *   10ms  水路时序、龙门 tick、仿真模型 tick
 *   50ms  报警桥接
 *   500ms 云端上报
 * 取最密的 10ms 档，模拟 3 个发布者各连续发布 20 拍。
 * ------------------------------------------------------------------------- */
static void test_steady_state_watermark(void)
{
    event_bus_stats_t stats;
    unsigned          round;
    unsigned          i;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_COMP_MOTION_COMPLETED, counting_handler));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, counting_handler));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_CHANGED, counting_handler));
    start_dispatch();

    for (round = 0U; round < 20U; round++) {
        for (i = 0U; i < 3U; i++) {
            (void)event_publish(EVT_COMP_MOTION_COMPLETED, i);
            (void)event_publish(EVT_ALARM_TRIGGERED, 201105U + i);
            (void)event_publish(EVT_OP_MODE_CHANGED, i);
        }
        usleep(10000U); /* 模拟 10ms 周期 */
    }

    usleep(50000U);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));

    printf("\n[容量实测] 稳态：published=%u dispatched=%u 普通队列峰值=%u/%u 高优先级峰值=%u/%u dropped=%u\n",
           stats.published_count,
           stats.dispatched_count,
           stats.queue_peak_depth,
           (unsigned)EVENT_BUS_QUEUE_SIZE,
           stats.hi_queue_peak_depth,
           (unsigned)EVENT_BUS_HI_QUEUE_SIZE,
           stats.dropped_count);

    /* 稳态下不应有任何丢弃 */
    TEST_ASSERT_EQUAL_UINT(0U, stats.dropped_count);
    /* 稳态水位应远低于容量：留出至少一半余量应对突发 */
    TEST_ASSERT_LESS_THAN_UINT(EVENT_BUS_QUEUE_SIZE / 2U, stats.queue_peak_depth);
}

/* -------------------------------------------------------------------------
 * 最坏情况：慢 handler 期间持续发布
 *
 * dispatch 单线程串行执行 handler，一个 handler 阻塞会让其后事件全部排队。
 * 本用例阻塞 handler 后连续发布至接近容量，验证：
 *   1. 队列填到接近上限时才开始丢弃（容量确实可用，不是提前失效）
 *   2. 丢弃有明确统计，不静默丢事件
 *   3. handler 解除阻塞后队列能排空恢复
 * ------------------------------------------------------------------------- */
static void test_slow_handler_backlog(void)
{
    event_bus_stats_t stats;
    unsigned          i;
    unsigned          accepted = 0U;
    unsigned          rejected = 0U;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_COMP_MOTION_COMPLETED, blocking_handler));
    start_dispatch();

    s_block_handler = 1;

    /* 先发一个进入 handler 并阻塞 */
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_COMP_MOTION_COMPLETED, 0U));
    while (!s_handler_entered) {
        usleep(1000U);
    }

    /* handler 阻塞期间持续发布，直到队列拒绝 */
    for (i = 0U; i < EVENT_BUS_QUEUE_SIZE + 10U; i++) {
        if (event_publish(EVT_COMP_MOTION_COMPLETED, i + 1U) == SW_OK) {
            accepted++;
        } else {
            rejected++;
        }
    }

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    printf("[容量实测] 慢 handler 积压：入队成功=%u 被拒=%u 普通队列峰值=%u/%u\n",
           accepted,
           rejected,
           stats.queue_peak_depth,
           (unsigned)EVENT_BUS_QUEUE_SIZE);

    /* 队列必须能装满标称容量才开始拒绝——若提前拒绝说明有效容量小于常量 */
    TEST_ASSERT_EQUAL_UINT(EVENT_BUS_QUEUE_SIZE, stats.queue_peak_depth);
    /* 超出的必须被拒绝且计入统计，不允许静默丢弃 */
    TEST_ASSERT_GREATER_THAN_UINT(0U, rejected);
    TEST_ASSERT_EQUAL_UINT(rejected, stats.dropped_count);

    /* 解除阻塞后应能排空 */
    s_block_handler = 0;
    usleep(200000U);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    printf("[容量实测] 解除阻塞后：当前深度=%u dispatched=%u\n", stats.queue_depth, stats.dispatched_count);
    TEST_ASSERT_EQUAL_UINT(0U, stats.queue_depth);
}

/* -------------------------------------------------------------------------
 * 高优先级队列独立性
 *
 * 普通队列积压时，安全事件必须仍能入队并优先分发——这是双队列设计的目的。
 * ------------------------------------------------------------------------- */
static void test_hi_queue_unaffected_by_normal_backlog(void)
{
    event_bus_stats_t stats;
    unsigned          i;
    unsigned          hi_accepted = 0U;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_COMP_MOTION_COMPLETED, blocking_handler));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, counting_handler));
    start_dispatch();

    s_block_handler = 1;
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_COMP_MOTION_COMPLETED, 0U));
    while (!s_handler_entered) {
        usleep(1000U);
    }

    /* 普通队列灌满 */
    for (i = 0U; i < EVENT_BUS_QUEUE_SIZE + 10U; i++) {
        (void)event_publish(EVT_COMP_MOTION_COMPLETED, i + 1U);
    }

    /* 普通队列已满，安全事件仍应能入队 */
    for (i = 0U; i < EVENT_BUS_HI_QUEUE_SIZE; i++) {
        if (event_publish(EVT_SAFETY_LOCKOUT, 0U) == SW_OK) {
            hi_accepted++;
        }
    }

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    printf("[容量实测] 普通队列满时高优先级入队=%u/%u 高优先级峰值=%u\n",
           hi_accepted,
           (unsigned)EVENT_BUS_HI_QUEUE_SIZE,
           stats.hi_queue_peak_depth);

    /* 高优先级队列不受普通队列影响，应全部接受 */
    TEST_ASSERT_EQUAL_UINT(EVENT_BUS_HI_QUEUE_SIZE, hi_accepted);

    s_block_handler = 0;
    usleep(200000U);
}

/* -------------------------------------------------------------------------
 * 订阅槽容量
 *
 * 单事件订阅者上限 8。实测 M8 上单个事件最多几个订阅者，用以判断余量。
 * ------------------------------------------------------------------------- */
static void h1(const event_t *e)
{
    (void)e;
}
static void h2(const event_t *e)
{
    (void)e;
}
static void h3(const event_t *e)
{
    (void)e;
}
static void h4(const event_t *e)
{
    (void)e;
}
static void h5(const event_t *e)
{
    (void)e;
}
static void h6(const event_t *e)
{
    (void)e;
}
static void h7(const event_t *e)
{
    (void)e;
}
static void h8(const event_t *e)
{
    (void)e;
}
static void h9(const event_t *e)
{
    (void)e;
}

static void test_subscriber_slot_capacity(void)
{
    event_handler_t handlers[] = {h1, h2, h3, h4, h5, h6, h7, h8};
    unsigned        i;

    start_dispatch();

    for (i = 0U; i < EVENT_BUS_MAX_SUBS_PER_EVT; i++) {
        TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, handlers[i]));
    }

    /* 第 9 个必须被拒绝且有明确错误码 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, event_subscribe(EVT_ALARM_TRIGGERED, h9));

    printf("[容量实测] 单事件订阅槽=%u（M8 实际最多 4 个订阅者：op_mode_bridge / "
           "telemetry_projection / lifecycle_bridge / 项目观测）\n",
           (unsigned)EVENT_BUS_MAX_SUBS_PER_EVT);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_steady_state_watermark, "", "验证稳态状态水位线");
    WDF_RUN_TEST(test_slow_handler_backlog, "", "验证慢速处理器积压");
    WDF_RUN_TEST(test_hi_queue_unaffected_by_normal_backlog, "", "验证HI队列不受影响按普通积压");
    WDF_RUN_TEST(test_subscriber_slot_capacity, "", "验证订阅者槽位容量");
    return UNITY_END();
}
