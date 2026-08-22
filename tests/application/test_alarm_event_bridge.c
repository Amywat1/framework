/**
 * @file    test_alarm_event_bridge.c
 * @brief   alarm_event_bridge 鍗曞厓娴嬭瘯
 */

#include "application/bridges/alarm_bridge.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"


static volatile int      g_trigger_count;
static volatile uint32_t g_last_code;
static volatile int      g_resync_count;
static volatile uint32_t g_resync_param;
static volatile int      g_cleared_count;

/* 201101 MANUAL_RESET：clear 只翻 condition_active，不删条目也不发 CLEARED。
 * 201709 AUTO_STATIC CRITICAL：一次 trigger + clear 产生两条域事件，且活动表
 * 始终 ≤1，用于在不触碰活跃池上限的前提下灌满待发队列、并驱动姿态边沿。 */
static const alarm_def_t s_catalog[] = {
    {
     .code         = 201101U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test",
     },
    {
     .code         = 201709U,
     .level        = ALARM_LEVEL_CRITICAL,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test critical",
     },
    {
     /* MINOR + AUTO_STATIC：trigger/clear 成对产生两条域事件，却不影响
      * blocking 与姿态，用作并发用例里的填充事件。 */
     .code         = 901001U,
     .level        = ALARM_LEVEL_MINOR,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test minor pad",
     },
};

#define CATALOG_COUNT (sizeof(s_catalog) / sizeof(s_catalog[0]))

static void on_triggered(const event_t *evt)
{
    g_trigger_count++;
    g_last_code = evt->param;
}

static void on_cleared(const event_t *evt)
{
    (void)evt;
    g_cleared_count++;
}

static void on_resync(const event_t *evt)
{
    g_resync_count++;
    g_resync_param = evt->param;
}

void setUp(void)
{
    g_trigger_count = 0;
    g_last_code     = 0U;
    g_resync_count  = 0;
    g_resync_param  = 0U;
    g_cleared_count = 0;
}

void tearDown(void)
{
}

static void test_drain_publishes_triggered_event(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    (void)alarm_registry_load_catalog(s_catalog, (unsigned)CATALOG_COUNT);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_init());
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_triggered);

    (void)alarm_registry_trigger(201101U);
    alarm_bridge_drain();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_UINT32(201101U, g_last_code);
}

static void test_drain_empty_queue_no_event(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    (void)alarm_registry_load_catalog(s_catalog, (unsigned)CATALOG_COUNT);
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_triggered);

    alarm_bridge_drain();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(0, g_trigger_count);
}

/* 待发队列溢出后必须发 EVT_ALARM_RESYNC，param 等于丢弃条数，且发在逐码事件之后
 * ——订阅者据此重读 registry，晚于逐码事件才能保证重读看到的是最终状态。 */
static void test_drain_publishes_resync_after_pending_overflow(void)
{
    unsigned cycles   = ALARM_PENDING_EVENT_MAX;  /* 每轮 2 条事件，必然溢出 */
    unsigned produced  = 2U * cycles;
    unsigned expected_dropped;
    unsigned i;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    (void)alarm_registry_load_catalog(s_catalog, (unsigned)CATALOG_COUNT);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_init());
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_triggered);
    (void)event_subscribe(EVT_ALARM_CLEARED, on_cleared);
    (void)event_subscribe(EVT_ALARM_RESYNC, on_resync);

    /* 全程不 drain，让 registry 的待发队列灌满并开始丢弃 */
    for (i = 0U; i < cycles; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201709U));
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(201709U));
    }
    expected_dropped = produced - ALARM_PENDING_EVENT_MAX;

    alarm_bridge_drain();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    /* 恰好发一条 RESYNC，param 是丢弃条数 */
    TEST_ASSERT_EQUAL_INT(1, g_resync_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)expected_dropped, g_resync_param);

    /* 入队的那些逐码事件仍全部发出，没有被 RESYNC 取代 */
    TEST_ASSERT_EQUAL_INT((int)ALARM_PENDING_EVENT_MAX, g_trigger_count + g_cleared_count);

    /* 丢弃计数已随上次取出清零：再 drain 一次不会重复发 RESYNC */
    alarm_bridge_drain();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, g_resync_count);
}

/* 没有溢出时不得发 RESYNC——否则订阅者每拍都要做一次无谓的全量重读 */
static void test_drain_without_overflow_publishes_no_resync(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    (void)alarm_registry_load_catalog(s_catalog, (unsigned)CATALOG_COUNT);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_init());
    (void)event_subscribe(EVT_ALARM_RESYNC, on_resync);

    (void)alarm_registry_trigger(201101U);
    alarm_bridge_drain();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(0, g_resync_count);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_drain_publishes_triggered_event, "", "验证排空发布触发事件");
    WDF_RUN_TEST(test_drain_empty_queue_no_event, "", "验证排空空队列无事件");
    WDF_RUN_TEST(test_drain_publishes_resync_after_pending_overflow,
                 "ALRM-13",
                 "验证待发队列溢出后发布重同步事件且参数为丢弃条数");
    WDF_RUN_TEST(test_drain_without_overflow_publishes_no_resync, "ALRM-13", "验证未溢出时不发布重同步事件");

    return UNITY_END();
}
