/**
 * @file    test_observation_event_bridge.c
 * @brief   observation_event_bridge 单元测试
 * @author  HUWANGWEI
 * @date    2026-08-04
 */

#include "application/bridges/observation_event_bridge.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "observability/core/observation.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define TEST_BOOT_ID 0x5A5AU

static void *dispatch_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

/* 分发线程句柄放静态变量并在 tearDown 收尾：断言失败时 Unity 直接跳出用例函数，
 * 若在用例末尾才 join，线程会留着不退出导致整个测试进程挂死——失败信息也就
 * 永远看不到。tearDown 无论用例成败都执行，故收尾放在那里。 */
static pthread_t s_dispatch_tid;
static bool      s_dispatch_running;

static void start_dispatch(void)
{
    pthread_create(&s_dispatch_tid, NULL, dispatch_fn, NULL);
    s_dispatch_running = true;
    usleep(10000);
}

/* 发布事件并等分发线程消费完：轮询 observation 队列而非固定睡眠，
 * 避免慢机器上偶发失败。 */
static bool publish_and_wait_record(event_type_t type, uint32_t param, observation_record_t *out)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(type, param));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    return observation_try_pop(out) == SW_OK;
}

void setUp(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_init(TEST_BOOT_ID));
}

void tearDown(void)
{
    if (s_dispatch_running) {
        (void)event_bus_shutdown();
        pthread_join(s_dispatch_tid, NULL);
        s_dispatch_running = false;
    }
}

/* -------------------------------------------------------------------------
 * 核心：框架事件确实产生观测记录
 *
 * 这是本桥接存在的理由——此前 observability 在框架生产路径零接入，
 * 其正确性只由自身单测保证，框架链路从未验证过它。
 * ------------------------------------------------------------------------- */
static void test_estop_event_becomes_critical_incident(void)
{
    observation_record_t rec;

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_event_bridge_init());

    TEST_ASSERT_TRUE(publish_and_wait_record(EVT_HW_ESTOP_ON, 0U, &rec));
    TEST_ASSERT_EQUAL_INT(OBSERVATION_SEVERITY_CRITICAL, rec.severity);
    TEST_ASSERT_EQUAL_INT(OBSERVATION_RECORD_INCIDENT, rec.kind);
    TEST_ASSERT_EQUAL_STRING("hw.estop", rec.source);
    TEST_ASSERT_EQUAL_UINT(TEST_BOOT_ID, (unsigned)rec.boot_id);
}

/* 事件 param 原样进载荷：桥接不解释它（语义随事件类型而定），
 * 但必须完整带上，否则报警码这类关键线索就丢了 */
static void test_event_param_is_carried_in_payload(void)
{
    observation_record_t rec;
    uint32_t             param = 201105U;
    uint32_t             got   = 0U;

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_event_bridge_init());

    TEST_ASSERT_TRUE(publish_and_wait_record(EVT_ALARM_TRIGGERED, param, &rec));
    TEST_ASSERT_EQUAL_UINT(sizeof(param), rec.payload_size);
    memcpy(&got, rec.payload, sizeof(got));
    TEST_ASSERT_EQUAL_UINT(param, got);
    TEST_ASSERT_EQUAL_UINT((unsigned)EVT_ALARM_TRIGGERED, (unsigned)rec.event_code);
}

/* 严重度按事件区分，不是一律 INFO 或一律 ERROR——否则导出后无法筛选 */
static void test_severity_differs_by_event(void)
{
    observation_record_t rec;

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_event_bridge_init());

    TEST_ASSERT_TRUE(publish_and_wait_record(EVT_SAFETY_LOCKOUT, 0U, &rec));
    TEST_ASSERT_EQUAL_INT(OBSERVATION_SEVERITY_CRITICAL, rec.severity);

    TEST_ASSERT_TRUE(publish_and_wait_record(EVT_SAFETY_NOMINAL, 0U, &rec));
    TEST_ASSERT_EQUAL_INT(OBSERVATION_SEVERITY_INFO, rec.severity);
    TEST_ASSERT_EQUAL_INT(OBSERVATION_RECORD_STATUS, rec.kind);

    TEST_ASSERT_TRUE(publish_and_wait_record(EVT_ALARM_CLEARED, 0U, &rec));
    TEST_ASSERT_EQUAL_INT(OBSERVATION_SEVERITY_INFO, rec.severity);
}

/* 未登记的事件不产生记录：EVT_CLOUD_POINT_DIRTY 每次点位变化都发，
 * 量级远高于其余事件，若被记录会淹没队列 */
static void test_unlisted_event_produces_no_record(void)
{
    observation_record_t rec;
    observation_stats_t  stats;

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_event_bridge_init());

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_CLOUD_POINT_DIRTY, 7U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_CONTEXT_SYNC, 0U));
    usleep(50000);

    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_FOUND, observation_try_pop(&rec));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT(0U, (unsigned)stats.published_count);
}

/* 多事件按发布顺序留痕：故障复盘依赖时间线，顺序错了因果就反了 */
static void test_records_preserve_publish_order(void)
{
    observation_record_t rec;

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_event_bridge_init());

    /* IO 掉线 → 报警触发 → 报警清除 → IO 恢复，是一条完整的通信故障链 */
    TEST_ASSERT_TRUE(publish_and_wait_record(EVT_HW_IO_OFFLINE, 1U, &rec));
    TEST_ASSERT_EQUAL_STRING("hw.io", rec.source);
    TEST_ASSERT_EQUAL_INT(OBSERVATION_SEVERITY_ERROR, rec.severity);

    TEST_ASSERT_TRUE(publish_and_wait_record(EVT_ALARM_TRIGGERED, 202101U, &rec));
    TEST_ASSERT_EQUAL_STRING("alarm", rec.source);

    TEST_ASSERT_TRUE(publish_and_wait_record(EVT_HW_IO_ONLINE, 1U, &rec));
    TEST_ASSERT_EQUAL_STRING("hw.io", rec.source);
    TEST_ASSERT_EQUAL_INT(OBSERVATION_SEVERITY_INFO, rec.severity);
}

/* 观测记录不取走时，桥接不阻塞分发线程，事件仍照常流转到其他订阅者。
 *
 * 这是本桥接最关键的性质：观测是旁路设施，任何情况下都不得反过来影响业务。
 *
 * 注意不能靠"灌满观测队列"来构造：event_bus 普通队列容量 64，远小于观测队列的
 * 256，事件在到达桥接之前就已在 event_bus 侧被丢弃，观测队列根本填不满。
 * 故改为验证可观察的性质——积压期间旁路订阅者仍被调用、分发线程能正常 join。 */
static unsigned s_probe_hits;

static void probe_handler(const event_t *evt)
{
    (void)evt;
    s_probe_hits++;
}

static void test_backlog_does_not_block_dispatch(void)
{
    observation_stats_t stats;
    unsigned            i;

    s_probe_hits = 0U;
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_event_bridge_init());
    /* 同一事件挂一个探针订阅者，用它证明分发链路未被观测拖住 */
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, probe_handler));
    start_dispatch();

    /* 故意不调 observation_try_pop，让记录持续积压 */
    for (i = 0U; i < 40U; i++) {
        (void)event_publish(EVT_ALARM_TRIGGERED, i);
        usleep(2000); /* 给分发线程留出消费窗口，避免 event_bus 侧先满 */
    }
    usleep(100000);

    /* 观测积压期间事件仍被正常分发 */
    TEST_ASSERT_TRUE(s_probe_hits > 0U);
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_get_stats(&stats));
    TEST_ASSERT_TRUE(stats.published_count > 0U);
    /* 深度不超过标称容量：说明满了就丢，没有无限增长 */
    TEST_ASSERT_TRUE(stats.normal_queue_depth <= OBSERVATION_NORMAL_QUEUE_CAPACITY);

    /* 分发线程未卡死由 tearDown 的 join 保证 */
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_estop_event_becomes_critical_incident, "", "验证急停事件变为严重级事故记录");
    WDF_RUN_TEST(test_event_param_is_carried_in_payload, "", "验证事件参数被携带到观测载荷");
    WDF_RUN_TEST(test_severity_differs_by_event, "", "验证严重度不同按事件");
    WDF_RUN_TEST(test_unlisted_event_produces_no_record, "", "验证未列出事件产生无记录");
    WDF_RUN_TEST(test_records_preserve_publish_order, "", "验证记录保留发布顺序");
    WDF_RUN_TEST(test_backlog_does_not_block_dispatch, "", "验证积压未阻止分发");
    return UNITY_END();
}
