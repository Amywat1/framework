/**
 * @file    test_alarm_direct_publish_adversarial.c
 * @brief   alarm-direct-publish 对抗式测试
 *
 * @note    以破坏者角色覆盖状态机边界、时序竞态、资源枯竭、重入、故障组合、
 *          边界值与初始化依赖。不修改被测源码。每个用例标注攻击目标与手法，
 *          且彼此独立。
 */

#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "domain/safety/model/safety_matrix.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/event_bus/event_bus_config.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <stdint.h>

#ifdef EVT_ALARM_RESYNC
#error INV-05 被打破：EVT_ALARM_RESYNC 必须已删除
#endif

enum {
    ADV_RACE_ROUNDS  = 200, /**< 并发 trigger/clear 轮次 */
    ADV_READER_SPINS = 800, /**< 视图读线程最大自旋次数 */
    ADV_SEQ_CAP      = 64   /**< 投递序列记录容量 */
};

static volatile int g_lockout_count;
static volatile int g_nominal_count;
static volatile int g_trigger_count;
static volatile int g_cleared_count;
static volatile int g_fatal_count;
static volatile int g_fatal_reason;
static volatile int g_view_too_many;
static volatile int g_lockout_without_crit;
static volatile int g_view_mismatch;
static volatile int g_stop;
static volatile int g_worker_rc;

static int      g_reenter_kind; /**< 0 无；1 LOCKOUT 内 trigger；2 TRIGGERED 内拷视图；3 LOCKOUT 内 clear */
static uint32_t g_reenter_code;
static uint32_t g_code_a;
static uint32_t g_code_b;

static event_type_t g_seq_type[ADV_SEQ_CAP];
static unsigned     g_seq_n;

static pthread_barrier_t s_barrier;

/**
 * @brief  必达发布失败回调：只计数，便于单测观察，不终止进程
 */
static void on_fatal(event_bus_fatal_reason_t reason, int detail_code)
{
    (void)detail_code;
    g_fatal_count++;
    g_fatal_reason = (int)reason;
}

static void seq_push(event_type_t type)
{
    if (g_seq_n < (unsigned)ADV_SEQ_CAP) {
        g_seq_type[g_seq_n++] = type;
    }
}

static void on_triggered(const event_t *evt)
{
    g_trigger_count++;
    seq_push(evt->type);
    if (g_reenter_kind == 2) {
        alarm_safety_view_t view;

        if (alarm_registry_copy_safety_view(&view) != SW_OK) {
            g_view_too_many = 1;
        } else if (view.count > ALARM_ACTIVE_MAX) {
            g_view_too_many = 1;
        }
    }
}

static void on_cleared(const event_t *evt)
{
    g_cleared_count++;
    seq_push(evt->type);
}

static void on_lockout(const event_t *evt)
{
    (void)evt;
    g_lockout_count++;
    seq_push(evt->type);
    if ((g_reenter_kind == 1) && (g_reenter_code != 0U) && (g_lockout_count == 1)) {
        (void)alarm_registry_trigger(g_reenter_code);
    } else if ((g_reenter_kind == 3) && (g_code_a != 0U) && (g_lockout_count == 1)) {
        (void)alarm_registry_clear(g_code_a);
    }
}

static void on_nominal(const event_t *evt)
{
    (void)evt;
    g_nominal_count++;
    seq_push(evt->type);
}

static alarm_def_t make_def(uint32_t code, alarm_level_t level, alarm_clear_t clear,
                            motion_reeval_group_id_t group)
{
    return (alarm_def_t){
        .code         = code,
        .level        = level,
        .clear        = clear,
        .reeval_group = group,
        .desc         = "adv",
    };
}

static uint32_t fill_code(unsigned i)
{
    uint32_t code = 0U;

    TEST_ASSERT_TRUE(alarm_code_make_checked(ALM_C_SW, i, ALM_N_OTHER, &code));
    return code;
}

static void subscribe_all(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, on_triggered));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_CLEARED, on_cleared));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_NOMINAL, on_nominal));
}

static void drain_bus(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
}

void setUp(void)
{
    g_lockout_count        = 0;
    g_nominal_count        = 0;
    g_trigger_count        = 0;
    g_cleared_count        = 0;
    g_fatal_count          = 0;
    g_fatal_reason         = 0;
    g_view_too_many        = 0;
    g_lockout_without_crit = 0;
    g_view_mismatch        = 0;
    g_stop                 = 0;
    g_worker_rc            = 0;
    g_reenter_kind         = 0;
    g_reenter_code         = 0U;
    g_seq_n                = 0U;
    g_code_a               = 0U;
    g_code_b               = 0U;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    event_bus_set_fatal_cb(on_fatal);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
}

void tearDown(void)
{
    event_bus_set_fatal_cb(NULL);
}

/* -------------------------------------------------------------------------
 * TC-01 状态机边界
 * 攻击目标: 重复 trigger 不得再发 TRIGGERED；第二条 CRITICAL 不得再发 LOCKOUT
 * 攻击手法: 连打同一 CRITICAL，再插入另一 CRITICAL；返回前用统计确认已入队
 * ------------------------------------------------------------------------- */
static void test_repeat_trigger_and_second_critical_no_extra_lockout(void)
{
    alarm_def_t       cat[2];
    event_bus_stats_t st;
    uint32_t          c1 = fill_code(1U);
    uint32_t          c2 = fill_code(2U);

    cat[0] = make_def(c1, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    cat[1] = make_def(c2, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 2U));
    subscribe_all();

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(c1));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&st));
    TEST_ASSERT_TRUE(st.queue_depth >= 1U);
    TEST_ASSERT_TRUE(st.hi_queue_depth >= 1U);
    TEST_ASSERT_EQUAL_INT(0, g_lockout_count);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());

    drain_bus();
    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(c1));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
    TEST_ASSERT_TRUE(alarm_registry_is_active(c1));

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(c2));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(2, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
    TEST_ASSERT_EQUAL_INT(0, g_nominal_count);
    TEST_ASSERT_TRUE(alarm_registry_is_active(c2));
}

/* -------------------------------------------------------------------------
 * TC-02 状态机边界
 * 攻击目标: load_catalog / init 清空活动表时不得补发 CLEARED 或 NOMINAL
 * 攻击手法: 先触发 CRITICAL，排空后再换目录 / 再 init
 * ------------------------------------------------------------------------- */
static void test_load_catalog_and_init_do_not_emit_cleared(void)
{
    alarm_def_t cat[1];
    uint32_t    c1 = fill_code(3U);

    cat[0] = make_def(c1, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 1U));
    subscribe_all();

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(c1));
    drain_bus();
    g_trigger_count = 0;
    g_cleared_count = 0;
    g_lockout_count = 0;
    g_nominal_count = 0;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 1U));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(0, g_cleared_count);
    TEST_ASSERT_EQUAL_INT(0, g_nominal_count);
    TEST_ASSERT_FALSE(alarm_registry_is_active(c1));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(c1));
    drain_bus();
    g_cleared_count = 0;
    g_nominal_count = 0;
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    drain_bus();
    TEST_ASSERT_EQUAL_INT(0, g_cleared_count);
    TEST_ASSERT_EQUAL_INT(0, g_nominal_count);
    TEST_ASSERT_EQUAL_UINT(0U, alarm_registry_catalog_count());
}

/* -------------------------------------------------------------------------
 * TC-03 时序竞态
 * 攻击目标: LOCKOUT 入队时活动表已是 LOCKOUT；两线程各 trigger 一个 MINOR 不丢不重
 * 攻击手法: 返回前不 drain 即断言姿态；并发 trigger 两个 MINOR
 * ------------------------------------------------------------------------- */
static void *trigger_code_a(void *arg)
{
    (void)arg;
    pthread_barrier_wait(&s_barrier);
    if (alarm_registry_trigger(g_code_a) != SW_OK) {
        g_worker_rc = 1;
    }
    return NULL;
}

static void *trigger_code_b(void *arg)
{
    (void)arg;
    pthread_barrier_wait(&s_barrier);
    if (alarm_registry_trigger(g_code_b) != SW_OK) {
        g_worker_rc = 1;
    }
    return NULL;
}

static void test_concurrent_two_minors_and_lockout_table_already_set(void)
{
    alarm_def_t cat[3];
    pthread_t   ta;
    pthread_t   tb;
    uint32_t    crit = fill_code(10U);

    g_code_a = fill_code(11U);
    g_code_b = fill_code(12U);
    cat[0]   = make_def(crit, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    cat[1]   = make_def(g_code_a, ALARM_LEVEL_MINOR, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    cat[2]   = make_def(g_code_b, ALARM_LEVEL_MINOR, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 3U));
    subscribe_all();

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    TEST_ASSERT_EQUAL_INT(0, g_lockout_count);
    drain_bus();
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);

    g_trigger_count = 0;
    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&s_barrier, NULL, 2));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&ta, NULL, trigger_code_a, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&tb, NULL, trigger_code_b, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(ta, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(tb, NULL));
    (void)pthread_barrier_destroy(&s_barrier);

    TEST_ASSERT_EQUAL_INT(0, g_worker_rc);
    TEST_ASSERT_TRUE(alarm_registry_is_active(g_code_a));
    TEST_ASSERT_TRUE(alarm_registry_is_active(g_code_b));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(2, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
}

/* -------------------------------------------------------------------------
 * TC-04 时序竞态
 * 攻击目标: 同码 AUTO_STATIC CRITICAL 并发 trigger/clear 结束后，末次姿态事件须匹配事实源
 * 攻击手法: 两线程对打 200 轮，join 后 drain，比较最后一条 SAFETY 事件与活动表
 * ------------------------------------------------------------------------- */
static void *hammer_trigger(void *arg)
{
    unsigned i;

    (void)arg;
    pthread_barrier_wait(&s_barrier);
    for (i = 0U; i < (unsigned)ADV_RACE_ROUNDS; ++i) {
        (void)alarm_registry_trigger(g_code_a);
    }
    return NULL;
}

static void *hammer_clear(void *arg)
{
    unsigned i;

    (void)arg;
    pthread_barrier_wait(&s_barrier);
    for (i = 0U; i < (unsigned)ADV_RACE_ROUNDS; ++i) {
        (void)alarm_registry_clear(g_code_a);
    }
    return NULL;
}

static void test_concurrent_trigger_clear_last_safety_event_matches_table(void)
{
    alarm_def_t cat[1];
    pthread_t   tw;
    pthread_t   tc;
    int         last_safety = 0;
    unsigned    i;

    g_code_a = fill_code(20U);
    cat[0]   = make_def(g_code_a, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 1U));
    subscribe_all();

    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&s_barrier, NULL, 2));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&tw, NULL, hammer_trigger, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&tc, NULL, hammer_clear, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(tw, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(tc, NULL));
    (void)pthread_barrier_destroy(&s_barrier);

    drain_bus();

    if (alarm_registry_is_active(g_code_a)) {
        TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    } else {
        TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());
    }

    for (i = 0U; i < g_seq_n; ++i) {
        if (g_seq_type[i] == EVT_SAFETY_LOCKOUT) {
            last_safety = 1;
        } else if (g_seq_type[i] == EVT_SAFETY_NOMINAL) {
            last_safety = -1;
        }
    }
    if (last_safety != 0) {
        if (alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT) {
            TEST_ASSERT_EQUAL_INT(1, last_safety);
        } else {
            TEST_ASSERT_EQUAL_INT(-1, last_safety);
        }
    }
}

/* -------------------------------------------------------------------------
 * TC-05 资源枯竭
 * 攻击目标: HI 队列满时 CRITICAL 仍写入活动表；LOCKOUT 必达失败可见，不得假装已通知
 * 攻击手法: 用 ESTOP 填满 HI 队列后再 trigger CRITICAL
 * ------------------------------------------------------------------------- */
static void test_hi_queue_full_critical_keeps_table_fatal_visible(void)
{
    alarm_def_t       cat[1];
    event_bus_stats_t st;
    uint32_t          crit = fill_code(30U);
    unsigned          i;

    cat[0] = make_def(crit, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 1U));
    subscribe_all();

    for (i = 0U; i < EVENT_BUS_HI_QUEUE_SIZE; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_ON, i));
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&st));
    TEST_ASSERT_EQUAL_UINT(EVENT_BUS_HI_QUEUE_SIZE, st.hi_queue_depth);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    TEST_ASSERT_TRUE(g_fatal_count >= 1);
    TEST_ASSERT_EQUAL_INT((int)EVENT_BUS_FATAL_REQUIRED_PUBLISH, g_fatal_reason);

    drain_bus();
    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(0, g_lockout_count);
}

/* -------------------------------------------------------------------------
 * TC-06 资源枯竭
 * 攻击目标: 普通队列满时 MINOR 仍进入活动表；TRIGGERED 失败走 required 契约
 * 攻击手法: EVT_CMD_ORDER 填满普通队列后再 trigger MINOR
 * ------------------------------------------------------------------------- */
static void test_normal_queue_full_minor_keeps_table_fatal_visible(void)
{
    alarm_def_t cat[1];
    uint32_t    minor = fill_code(31U);
    unsigned    i;

    cat[0] = make_def(minor, ALARM_LEVEL_MINOR, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 1U));
    subscribe_all();

    for (i = 0U; i < EVENT_BUS_QUEUE_SIZE; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_CMD_ORDER, i));
    }

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(minor));
    TEST_ASSERT_TRUE(alarm_registry_is_active(minor));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());
    TEST_ASSERT_TRUE(g_fatal_count >= 1);
    TEST_ASSERT_EQUAL_INT((int)EVENT_BUS_FATAL_REQUIRED_PUBLISH, g_fatal_reason);

    drain_bus();
    TEST_ASSERT_EQUAL_INT(0, g_trigger_count);
}

/* -------------------------------------------------------------------------
 * TC-07 资源枯竭
 * 攻击目标: 全是 CRITICAL 时拒绝新 CRITICAL，不得驱逐、不得发 CLEARED/LOCKOUT
 * 攻击手法: 灌满 ALARM_ACTIVE_MAX 条 CRITICAL 后再 trigger 新码
 * ------------------------------------------------------------------------- */
static void test_full_lockout_pool_rejects_without_events(void)
{
    alarm_def_t cat[ALARM_ACTIVE_MAX + 1U];
    uint32_t    extra = fill_code(ALARM_ACTIVE_MAX);
    unsigned    i;

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_def(fill_code(i), ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    }
    cat[ALARM_ACTIVE_MAX] = make_def(extra, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U));
    subscribe_all();

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    drain_bus();
    g_trigger_count = 0;
    g_cleared_count = 0;
    g_lockout_count = 0;

    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, alarm_registry_trigger(extra));
    TEST_ASSERT_FALSE(alarm_registry_is_active(extra));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(0, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(0, g_cleared_count);
    TEST_ASSERT_EQUAL_INT(0, g_lockout_count);
    TEST_ASSERT_TRUE(alarm_registry_is_active(fill_code(0U)));
}

/* -------------------------------------------------------------------------
 * TC-08 重入与嵌套
 * 攻击目标: LOCKOUT handler 内再 trigger 第二条 CRITICAL 不得双发 LOCKOUT、不得自锁
 * 攻击手法: 订阅 LOCKOUT，回调里 trigger 另一 CRITICAL，由 drain 同步投递
 * ------------------------------------------------------------------------- */
static void test_lockout_handler_retrigger_second_critical(void)
{
    alarm_def_t cat[2];
    uint32_t    c1 = fill_code(40U);
    uint32_t    c2 = fill_code(41U);

    cat[0] = make_def(c1, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    cat[1] = make_def(c2, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 2U));

    g_reenter_kind = 1;
    g_reenter_code = c2;
    subscribe_all();

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(c1));
    drain_bus();

    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
    TEST_ASSERT_EQUAL_INT(2, g_trigger_count);
    TEST_ASSERT_TRUE(alarm_registry_is_active(c1));
    TEST_ASSERT_TRUE(alarm_registry_is_active(c2));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
}

/* -------------------------------------------------------------------------
 * TC-09 重入与嵌套
 * 攻击目标: TRIGGERED handler 中拷视图不得越界；LOCKOUT handler 中 clear 不得死锁
 * 攻击手法: 先用 TRIGGERED 回调拷视图，再单独走 LOCKOUT 回调 clear 同一 AUTO_STATIC
 * ------------------------------------------------------------------------- */
static void test_handler_copy_view_and_nested_clear_no_deadlock(void)
{
    alarm_def_t cat[2];
    uint32_t    minor = fill_code(42U);
    uint32_t    crit  = fill_code(43U);

    cat[0] = make_def(minor, ALARM_LEVEL_MINOR, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    cat[1] = make_def(crit, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 2U));

    g_reenter_kind = 2;
    subscribe_all();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(minor));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(0, g_view_too_many);
    TEST_ASSERT_TRUE(alarm_registry_is_active(minor));

    g_reenter_kind  = 3;
    g_code_a        = crit;
    g_lockout_count = 0;
    g_nominal_count = 0;
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    drain_bus();
    TEST_ASSERT_FALSE(alarm_registry_is_active(crit));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
    TEST_ASSERT_EQUAL_INT(1, g_nominal_count);
}

/* -------------------------------------------------------------------------
 * TC-10 故障组合
 * 攻击目标: 池满驱逐须同时发出 CLEARED+TRIGGERED+LOCKOUT；未知码不得搅乱已入队事件
 * 攻击手法: 32 MINOR 灌满后 CRITICAL 驱逐，紧接着 trigger 未知码
 * ------------------------------------------------------------------------- */
static void test_eviction_events_then_unknown_code_does_not_drop_queue(void)
{
    alarm_def_t       cat[ALARM_ACTIVE_MAX + 1U];
    event_bus_stats_t st;
    uint32_t          crit   = fill_code(ALARM_ACTIVE_MAX);
    uint32_t          victim = fill_code(0U);
    unsigned          i;

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_def(fill_code(i), ALARM_LEVEL_MINOR, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    }
    cat[ALARM_ACTIVE_MAX] = make_def(crit, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U));
    subscribe_all();

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    drain_bus();
    g_trigger_count = 0;
    g_cleared_count = 0;
    g_lockout_count = 0;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_FALSE(alarm_registry_is_active(victim));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&st));
    TEST_ASSERT_TRUE(st.queue_depth >= 2U);
    TEST_ASSERT_TRUE(st.hi_queue_depth >= 1U);

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_trigger(999999U));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(1, g_cleared_count);
    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
}

/* -------------------------------------------------------------------------
 * TC-11 故障组合
 * 攻击目标: journal 满且活动池满时 CRITICAL 仍准入；丢弃计数读不清零；MINOR 不增加丢弃
 * 攻击手法: 开会话，16 MAJOR 灌 journal，再 MINOR 灌满池，然后 CRITICAL，再拷两次视图
 * ------------------------------------------------------------------------- */
static void test_journal_and_pool_full_then_critical_dropped_sticky(void)
{
    alarm_def_t         cat[ALARM_ACTIVE_MAX + 2U];
    alarm_safety_view_t v1;
    alarm_safety_view_t v2;
    uint32_t            crit  = fill_code(ALARM_ACTIVE_MAX);
    uint32_t            minor = fill_code(ALARM_ACTIVE_MAX + 1U);
    unsigned            i;

    for (i = 0U; i < ALARM_SESSION_JOURNAL_MAX; ++i) {
        cat[i] = make_def(fill_code(i), ALARM_LEVEL_MAJOR, ALARM_CLEAR_MANUAL_RESET, ALARM_REEVAL_GROUP_NONE);
    }
    for (i = ALARM_SESSION_JOURNAL_MAX; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_def(fill_code(i), ALARM_LEVEL_MINOR, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    }
    cat[ALARM_ACTIVE_MAX]      = make_def(crit, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    cat[ALARM_ACTIVE_MAX + 1U] = make_def(minor, ALARM_LEVEL_MINOR, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 2U));
    alarm_registry_on_wash_session_started();
    subscribe_all();

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_FALSE(alarm_registry_is_active(fill_code(ALARM_SESSION_JOURNAL_MAX)));

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_copy_safety_view(&v1));
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX, v1.journal_count);
    TEST_ASSERT_TRUE(v1.journal_dropped >= 1U);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, v1.posture);
    TEST_ASSERT_TRUE(v1.blocking);

    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, alarm_registry_trigger(minor));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_copy_safety_view(&v2));
    TEST_ASSERT_EQUAL_UINT32(v1.journal_dropped, v2.journal_dropped);
    TEST_ASSERT_EQUAL_UINT(v1.journal_count, v2.journal_count);
}

/* -------------------------------------------------------------------------
 * TC-12 边界值
 * 攻击目标: 非法码、空视图、空 reset_all、空 reevaluate 不得发事件或改姿态
 * 攻击手法: code=0 / UINT32_MAX；copy NULL；reset_all 空表；reevaluate 空分组
 * ------------------------------------------------------------------------- */
static void test_boundary_codes_null_view_empty_reset_reeval(void)
{
    alarm_def_t         cat[2];
    alarm_safety_view_t view;
    uint32_t            on_motion = fill_code(50U);
    uint32_t            manual    = fill_code(51U);

    cat[0] = make_def(on_motion, ALARM_LEVEL_MAJOR, ALARM_CLEAR_ON_MOTION, (motion_reeval_group_id_t)1U);
    cat[1] = make_def(manual, ALARM_LEVEL_MAJOR, ALARM_CLEAR_MANUAL_RESET, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 2U));
    subscribe_all();

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_trigger(ALARM_CODE_NONE));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_trigger(UINT32_MAX));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_clear(UINT32_MAX));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_copy_safety_view(NULL));

    alarm_registry_reset_all();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_reevaluate_group((motion_reeval_group_id_t)1U));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(0, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(0, g_cleared_count);
    TEST_ASSERT_EQUAL_INT(0, g_lockout_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_copy_safety_view(&view));
    TEST_ASSERT_EQUAL_UINT(0U, view.count);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, view.posture);
    TEST_ASSERT_EQUAL_UINT(ALARM_CODE_NONE, view.top_code);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(on_motion));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_reevaluate_group((motion_reeval_group_id_t)1U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(on_motion));
    drain_bus();
    g_cleared_count = 0;
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(on_motion));
    TEST_ASSERT_TRUE(alarm_registry_is_active(on_motion));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(0, g_cleared_count);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_reevaluate_group((motion_reeval_group_id_t)1U));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(1, g_cleared_count);
    TEST_ASSERT_FALSE(alarm_registry_is_active(on_motion));
}

/* -------------------------------------------------------------------------
 * TC-13 初始化依赖
 * 攻击目标: 未装目录 trigger 失败；init 清目录后不得残留活动表；关总线后变位仍写入事实源
 * 攻击手法: 跳过 catalog；装载触发后再 init；shutdown 总线后 trigger
 * ------------------------------------------------------------------------- */
static void test_init_order_catalog_and_bus_shutdown(void)
{
    alarm_def_t cat[1];
    uint32_t    crit = fill_code(60U);

    subscribe_all();
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_trigger(crit));
    TEST_ASSERT_FALSE(alarm_registry_is_active(crit));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(0, g_trigger_count);

    cat[0] = make_def(crit, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_UINT(0U, alarm_registry_catalog_count());
    TEST_ASSERT_FALSE(alarm_registry_is_active(crit));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_shutdown());
    g_fatal_count = 0;
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    TEST_ASSERT_TRUE(g_fatal_count >= 1);
}

/* -------------------------------------------------------------------------
 * TC-14 时序竞态 / 视图
 * 攻击目标: 驱逐窗口内 copy_safety_view 不得 count>MAX，LOCKOUT 必须能找到 lockout 条目
 * 攻击手法: 读线程连续拷视图，写线程灌满 MINOR 后插入 CRITICAL
 * ------------------------------------------------------------------------- */
static void *view_reader(void *arg)
{
    unsigned n;

    (void)arg;
    pthread_barrier_wait(&s_barrier);
    for (n = 0U; (g_stop == 0) && (n < (unsigned)ADV_READER_SPINS); ++n) {
        alarm_safety_view_t view;
        unsigned            i;
        int                 found = 0;

        if (alarm_registry_copy_safety_view(&view) != SW_OK) {
            g_view_too_many = 1;
            continue;
        }
        if (view.count > ALARM_ACTIVE_MAX) {
            g_view_too_many = 1;
        }
        if (view.posture == SAFETY_POSTURE_LOCKOUT) {
            for (i = 0U; i < view.count; ++i) {
                if (alarm_level_forces_lockout(view.list[i].level)) {
                    found = 1;
                    break;
                }
            }
            if (found == 0) {
                g_lockout_without_crit = 1;
            }
        }
        if (view.blocking) {
            int blk = 0;

            for (i = 0U; i < view.count; ++i) {
                if (alarm_level_blocks_wash(view.list[i].level)) {
                    blk = 1;
                    break;
                }
            }
            if (blk == 0) {
                g_view_mismatch = 1;
            }
        }
    }
    return NULL;
}

static void test_copy_safety_view_stable_during_eviction(void)
{
    alarm_def_t cat[ALARM_ACTIVE_MAX + 1U];
    pthread_t   reader;
    uint32_t    crit = fill_code(ALARM_ACTIVE_MAX);
    unsigned    i;

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_def(fill_code(i), ALARM_LEVEL_MINOR, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    }
    cat[ALARM_ACTIVE_MAX] = make_def(crit, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, ALARM_REEVAL_GROUP_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U));

    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&s_barrier, NULL, 2));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&reader, NULL, view_reader, NULL));
    pthread_barrier_wait(&s_barrier);

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));

    g_stop = 1;
    TEST_ASSERT_EQUAL_INT(0, pthread_join(reader, NULL));
    (void)pthread_barrier_destroy(&s_barrier);

    TEST_ASSERT_EQUAL_INT(0, g_view_too_many);
    TEST_ASSERT_EQUAL_INT(0, g_lockout_without_crit);
    TEST_ASSERT_EQUAL_INT(0, g_view_mismatch);
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_repeat_trigger_and_second_critical_no_extra_lockout,
                 "",
                 "对抗：重复 trigger 与第二条 CRITICAL 不额外发 LOCKOUT");
    WDF_RUN_TEST(test_load_catalog_and_init_do_not_emit_cleared, "", "对抗：换目录与 init 不补发 CLEARED");
    WDF_RUN_TEST(test_concurrent_two_minors_and_lockout_table_already_set,
                 "",
                 "对抗：返回前姿态已变且并发 MINOR 不丢码");
    WDF_RUN_TEST(test_concurrent_trigger_clear_last_safety_event_matches_table,
                 "",
                 "对抗：同码并发 trigger/clear 末次姿态事件匹配事实源");
    WDF_RUN_TEST(test_hi_queue_full_critical_keeps_table_fatal_visible,
                 "",
                 "对抗：HI 队列满时活动表仍更新且失败可见");
    WDF_RUN_TEST(test_normal_queue_full_minor_keeps_table_fatal_visible,
                 "",
                 "对抗：普通队列满时 MINOR 仍入表且失败可见");
    WDF_RUN_TEST(test_full_lockout_pool_rejects_without_events, "", "对抗：满员 CRITICAL 拒绝新码且不发事件");
    WDF_RUN_TEST(test_lockout_handler_retrigger_second_critical, "", "对抗：LOCKOUT 回调内再 trigger 不双发");
    WDF_RUN_TEST(test_handler_copy_view_and_nested_clear_no_deadlock,
                 "",
                 "对抗：handler 内拷视图与嵌套 clear 不死锁");
    WDF_RUN_TEST(test_eviction_events_then_unknown_code_does_not_drop_queue,
                 "",
                 "对抗：驱逐三件套入队后未知码不得丢队列");
    WDF_RUN_TEST(test_journal_and_pool_full_then_critical_dropped_sticky,
                 "",
                 "对抗：journal 与池同时满时 CRITICAL 准入且丢弃不清零");
    WDF_RUN_TEST(test_boundary_codes_null_view_empty_reset_reeval, "", "对抗：非法码与空复位/重评估边界");
    WDF_RUN_TEST(test_init_order_catalog_and_bus_shutdown, "", "对抗：未装目录、再 init、关总线后事实源仍更新");
    WDF_RUN_TEST(test_copy_safety_view_stable_during_eviction, "", "对抗：驱逐同时拷视图不越界且姿态自洽");

    return UNITY_END();
}
