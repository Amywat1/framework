/**
 * @file    test_alarm_critical_path_adversarial.c
 * @brief   tighten-alarm-critical-path 对抗式测试
 *
 * @note    以破坏者角色覆盖立即 drain、lockout 驱逐与会话 journal。
 *          不修改被测源码。每个用例标注攻击目标与手法，且彼此独立。
 */

#include "application/bridges/alarm_binding_bridge.h"
#include "application/bridges/alarm_bridge.h"
#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "domain/safety/model/safety_matrix.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/ports/port_registry.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <stddef.h>
#include <time.h>

#define CRIT_CODE 201709U
#define CRIT2_CODE 201809U
#define MINOR_PAD_CODE 901001U
#define UNKNOWN_CODE 999999U

static volatile int      g_lockout_count;
static volatile int      g_nominal_count;
static volatile int      g_trigger_count;
static volatile int      g_cleared_count;
static volatile int      g_resync_count;
static volatile uint32_t g_resync_param;

static const alarm_binding_ops_t *g_ops;
static uint32_t                   g_reenter_code;

static pthread_barrier_t s_barrier;
static volatile int      g_stop;
static volatile int      g_view_too_many;
static volatile int      g_lockout_without_crit;
static volatile unsigned g_view_max;

static alarm_def_t make_fill_def(uint32_t code, alarm_level_t level)
{
    return (alarm_def_t){
        .code         = code,
        .level        = level,
        .clear        = ALARM_CLEAR_AUTO_STATIC,
        .reeval_group = ALARM_REEVAL_GROUP_NONE,
        .desc         = "adv",
    };
}

static uint32_t fill_code(unsigned i)
{
    uint32_t code = 0U;

    TEST_ASSERT_TRUE(alarm_code_make_checked(ALM_C_SW, i, ALM_N_OTHER, &code));
    return code;
}

static const alarm_binding_ops_t *ops(void)
{
    const alarm_binding_ops_t *p = alarm_binding_get_ops();

    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NOT_NULL(p->trigger);
    TEST_ASSERT_NOT_NULL(p->clear);
    TEST_ASSERT_NOT_NULL(p->load_catalog);
    return p;
}

static void on_lockout(const event_t *evt)
{
    (void)evt;
    g_lockout_count++;
}

static void on_nominal(const event_t *evt)
{
    (void)evt;
    g_nominal_count++;
}

static void on_triggered(const event_t *evt)
{
    (void)evt;
    g_trigger_count++;
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

/**
 * @brief  LOCKOUT 回调内再 trigger 另一条 CRITICAL，攻击重入
 */
static void on_lockout_reenter(const event_t *evt)
{
    (void)evt;
    g_lockout_count++;
    if ((g_lockout_count == 1) && (g_ops != NULL) && (g_reenter_code != 0U)) {
        (void)g_ops->trigger(g_reenter_code);
    }
}

static void linger_in_drain_cs(void)
{
    struct timespec ts = {0, 2000000L};

    (void)nanosleep(&ts, NULL);
}

void setUp(void)
{
    g_lockout_count          = 0;
    g_nominal_count          = 0;
    g_trigger_count          = 0;
    g_cleared_count          = 0;
    g_resync_count           = 0;
    g_resync_param           = 0U;
    g_ops                    = NULL;
    g_reenter_code           = 0U;
    g_stop                   = 0;
    g_view_too_many          = 0;
    g_lockout_without_crit   = 0;
    g_view_max               = 0U;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    port_registry_infra_reset();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_binding_bridge_bind());
    alarm_bridge_reset_for_test();
}

void tearDown(void)
{
}

/* -------------------------------------------------------------------------
 * TC-01 状态机边界
 * 攻击目标: 清除最后一条 CRITICAL 后命令路径已 NOMINAL，但立即 drain 被跳过
 * 攻击手法: binding clear 后先 event_bus_drain，再显式 drain
 * ------------------------------------------------------------------------- */
static void test_clear_last_critical_delays_nominal_event(void)
{
    const alarm_binding_ops_t *p = ops();
    alarm_def_t                cat[] = {make_fill_def(CRIT_CODE, ALARM_LEVEL_CRITICAL)};

    TEST_ASSERT_EQUAL_INT(SW_OK, p->load_catalog(cat, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_NOMINAL, on_nominal));

    TEST_ASSERT_EQUAL_INT(SW_OK, p->trigger(CRIT_CODE));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, p->clear(CRIT_CODE));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());
    TEST_ASSERT_FALSE(alarm_registry_is_active(CRIT_CODE));

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(0, g_nominal_count);

    alarm_bridge_drain();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, g_nominal_count);
}

/* -------------------------------------------------------------------------
 * TC-02 状态机边界
 * 攻击目标: 驱逐应优先 MINOR，不得因 MAJOR 更早插入而误逐 MAJOR
 * 攻击手法: 1 条 MAJOR + 31 条 MINOR 灌满后再 trigger CRITICAL
 * ------------------------------------------------------------------------- */
static void test_eviction_prefers_minor_over_older_major(void)
{
    alarm_def_t cat[ALARM_ACTIVE_MAX + 1U];
    uint32_t    major = fill_code(0U);
    uint32_t    crit  = fill_code(ALARM_ACTIVE_MAX);
    unsigned    i;

    cat[0] = make_fill_def(major, ALARM_LEVEL_MAJOR);
    for (i = 1U; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MINOR);
    }
    cat[ALARM_ACTIVE_MAX] = make_fill_def(crit, ALARM_LEVEL_CRITICAL);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U));
    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(major));
    TEST_ASSERT_FALSE(alarm_registry_is_active(fill_code(1U)));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
}

/* -------------------------------------------------------------------------
 * TC-03 状态机边界
 * 攻击目标: 被驱逐 MINOR 立刻再 trigger 不得挤掉已准入的 lockout
 * 攻击手法: 池满驱逐后立即重报被逐码
 * ------------------------------------------------------------------------- */
static void test_retrigger_evicted_minor_does_not_kick_lockout(void)
{
    alarm_def_t cat[ALARM_ACTIVE_MAX + 1U];
    uint32_t    victim = fill_code(0U);
    uint32_t    crit   = fill_code(ALARM_ACTIVE_MAX);
    unsigned    i;

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MINOR);
    }
    cat[ALARM_ACTIVE_MAX] = make_fill_def(crit, ALARM_LEVEL_CRITICAL);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U));
    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_FALSE(alarm_registry_is_active(victim));
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, alarm_registry_trigger(victim));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_FALSE(alarm_registry_is_active(victim));
}

/* -------------------------------------------------------------------------
 * TC-04 时序竞态 / INV-03
 * 攻击目标: 立即 drain 只入队，不得在采集线程同步执行 LOCKOUT handler
 * 攻击手法: trigger 返回后、event_bus_drain 前检查 handler 计数与高优先级队列
 * ------------------------------------------------------------------------- */
static void test_lockout_enqueued_before_handlers_run(void)
{
    const alarm_binding_ops_t *p = ops();
    alarm_def_t                cat[] = {make_fill_def(CRIT_CODE, ALARM_LEVEL_CRITICAL)};
    event_bus_stats_t          st;

    TEST_ASSERT_EQUAL_INT(SW_OK, p->load_catalog(cat, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout));

    TEST_ASSERT_EQUAL_INT(SW_OK, p->trigger(CRIT_CODE));
    TEST_ASSERT_EQUAL_INT(0, g_lockout_count);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&st));
    TEST_ASSERT_TRUE(st.hi_queue_depth >= 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, st.dispatched_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
}

/* -------------------------------------------------------------------------
 * TC-05 时序竞态
 * 攻击目标: 驱逐窗口内 copy_safety_view 不得读到 count>MAX 或 LOCKOUT 无 lockout 条目
 * 攻击手法: 读线程连续拷贝，写线程灌满 MINOR 后插入 CRITICAL
 * ------------------------------------------------------------------------- */
static void *safety_view_reader(void *arg)
{
    (void)arg;
    pthread_barrier_wait(&s_barrier);
    while (g_stop == 0) {
        alarm_instance_t list[ALARM_ACTIVE_MAX];
        bool             blocking = false;
        uint32_t         top      = ALARM_CODE_NONE;
        safety_posture_t posture  = SAFETY_POSTURE_NOMINAL;
        unsigned         n;
        unsigned         i;
        int              found = 0;

        n = alarm_registry_copy_safety_view(list, ALARM_ACTIVE_MAX, &blocking, &top, &posture);
        if (n > ALARM_ACTIVE_MAX) {
            g_view_too_many = 1;
        }
        if (n > g_view_max) {
            g_view_max = n;
        }
        if (posture == SAFETY_POSTURE_LOCKOUT) {
            for (i = 0U; i < n; ++i) {
                if (alarm_level_forces_lockout(list[i].level)) {
                    found = 1;
                    break;
                }
            }
            if (found == 0) {
                g_lockout_without_crit = 1;
            }
        }
    }
    return NULL;
}

static void test_copy_safety_view_stable_during_lockout_eviction(void)
{
    alarm_def_t cat[ALARM_ACTIVE_MAX + 1U];
    pthread_t   reader;
    uint32_t    crit = fill_code(ALARM_ACTIVE_MAX);
    unsigned    i;

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MINOR);
    }
    cat[ALARM_ACTIVE_MAX] = make_fill_def(crit, ALARM_LEVEL_CRITICAL);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U));

    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&s_barrier, NULL, 2));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&reader, NULL, safety_view_reader, NULL));
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
    TEST_ASSERT_TRUE(g_view_max <= ALARM_ACTIVE_MAX);
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
}

/* -------------------------------------------------------------------------
 * TC-06 时序竞态
 * 攻击目标: binding 立即 drain 与另一线程 drain 临界区重叠，或双发 LOCKOUT
 * 攻击手法: 一线程循环 drain（临界区停留），另一线程 binding trigger CRITICAL
 * ------------------------------------------------------------------------- */
static void *drain_loop_thread(void *arg)
{
    unsigned n = *(unsigned *)arg;
    unsigned i;

    pthread_barrier_wait(&s_barrier);
    for (i = 0U; i < n; ++i) {
        alarm_bridge_drain();
    }
    return NULL;
}

static void test_binding_drain_and_periodic_drain_do_not_overlap(void)
{
    const alarm_binding_ops_t *p = ops();
    alarm_def_t                cat[] = {make_fill_def(CRIT_CODE, ALARM_LEVEL_CRITICAL)};
    pthread_t                  drainer;
    unsigned                   iters = 40U;

    TEST_ASSERT_EQUAL_INT(SW_OK, p->load_catalog(cat, 1U));
    alarm_bridge_test_set_in_cs_hook(linger_in_drain_cs);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout));

    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&s_barrier, NULL, 2));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&drainer, NULL, drain_loop_thread, &iters));
    pthread_barrier_wait(&s_barrier);
    TEST_ASSERT_EQUAL_INT(SW_OK, p->trigger(CRIT_CODE));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(drainer, NULL));
    (void)pthread_barrier_destroy(&s_barrier);

    TEST_ASSERT_EQUAL_INT(0, alarm_bridge_test_cs_overlap());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
}

/* -------------------------------------------------------------------------
 * TC-07 资源枯竭
 * 攻击目标: pending 已满时驱逐仍须准入 lockout；消费者须得知不完整
 * 攻击手法: 32 条 MINOR 灌满 pending 后 binding trigger CRITICAL
 * ------------------------------------------------------------------------- */
static void test_eviction_when_pending_full_still_admits_lockout(void)
{
    const alarm_binding_ops_t *p = ops();
    alarm_def_t                cat[ALARM_ACTIVE_MAX + 1U];
    alarm_domain_event_t       leftover[4];
    uint32_t                   dropped = 0U;
    uint32_t                   crit    = fill_code(ALARM_ACTIVE_MAX);
    unsigned                   i;
    unsigned                   n;

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MINOR);
    }
    cat[ALARM_ACTIVE_MAX] = make_fill_def(crit, ALARM_LEVEL_CRITICAL);
    TEST_ASSERT_EQUAL_INT(SW_OK, p->load_catalog(cat, ALARM_ACTIVE_MAX + 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_RESYNC, on_resync));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_CLEARED, on_cleared));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout));

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }

    TEST_ASSERT_EQUAL_INT(SW_OK, p->trigger(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_FALSE(alarm_registry_is_active(fill_code(0U)));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());

    n = alarm_registry_pull_events(leftover, 4U, &dropped);
    TEST_ASSERT_EQUAL_UINT(0U, n);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
    TEST_ASSERT_TRUE((g_resync_count >= 1) || (g_cleared_count >= 1));
    TEST_ASSERT_TRUE(g_resync_param >= 1U || g_cleared_count >= 1);
}

/* -------------------------------------------------------------------------
 * TC-08 资源枯竭 + 故障组合
 * 攻击目标: journal 满且活跃池满时 CRITICAL 仍准入，journal 丢弃可见，MINOR 被逐
 * 攻击手法: 16 MAJOR 灌满 journal，再 16 MINOR 灌满池，然后 CRITICAL
 * ------------------------------------------------------------------------- */
static void test_journal_and_pool_full_then_critical(void)
{
    alarm_def_t cat[ALARM_ACTIVE_MAX + 1U];
    uint32_t    codes[ALARM_SESSION_JOURNAL_MAX];
    uint32_t    dropped = 0U;
    uint32_t    crit    = fill_code(ALARM_ACTIVE_MAX);
    unsigned    i;
    unsigned    n;

    for (i = 0U; i < ALARM_SESSION_JOURNAL_MAX; ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MAJOR);
    }
    for (i = ALARM_SESSION_JOURNAL_MAX; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MINOR);
    }
    cat[ALARM_ACTIVE_MAX] = make_fill_def(crit, ALARM_LEVEL_CRITICAL);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U));
    alarm_registry_on_wash_session_started();

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));

    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(fill_code(0U)));
    TEST_ASSERT_FALSE(alarm_registry_is_active(fill_code(ALARM_SESSION_JOURNAL_MAX)));

    n = alarm_registry_get_session_journal(codes, ALARM_SESSION_JOURNAL_MAX, &dropped);
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX, n);
    TEST_ASSERT_TRUE(dropped >= 1U);
}

/* -------------------------------------------------------------------------
 * TC-09 重入与嵌套
 * 攻击目标: LOCKOUT handler 内再 trigger 第二条 CRITICAL 不得双发 LOCKOUT、不得自锁
 * 攻击手法: 订阅 LOCKOUT，回调里 binding trigger 另一 CRITICAL
 * ------------------------------------------------------------------------- */
static void test_lockout_handler_retrigger_second_critical(void)
{
    const alarm_binding_ops_t *p = ops();
    alarm_def_t                cat[2];

    cat[0] = make_fill_def(CRIT_CODE, ALARM_LEVEL_CRITICAL);
    cat[1] = make_fill_def(CRIT2_CODE, ALARM_LEVEL_CRITICAL);
    TEST_ASSERT_EQUAL_INT(SW_OK, p->load_catalog(cat, 2U));

    g_ops          = p;
    g_reenter_code = CRIT2_CODE;
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout_reenter));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, on_triggered));

    TEST_ASSERT_EQUAL_INT(SW_OK, p->trigger(CRIT_CODE));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
    TEST_ASSERT_TRUE(alarm_registry_is_active(CRIT_CODE));
    TEST_ASSERT_TRUE(alarm_registry_is_active(CRIT2_CODE));
    TEST_ASSERT_EQUAL_INT(2, g_trigger_count);
}

/* -------------------------------------------------------------------------
 * TC-10 故障组合
 * 攻击目标: 未知码失败不得误 drain，已有 MINOR pending 仍留在 registry
 * 攻击手法: registry trigger MINOR 后 binding trigger 未知码
 * ------------------------------------------------------------------------- */
static void test_unknown_trigger_does_not_drain_pending_minor(void)
{
    const alarm_binding_ops_t *p = ops();
    alarm_def_t                cat[2];

    cat[0] = make_fill_def(MINOR_PAD_CODE, ALARM_LEVEL_MINOR);
    cat[1] = make_fill_def(CRIT_CODE, ALARM_LEVEL_CRITICAL);
    TEST_ASSERT_EQUAL_INT(SW_OK, p->load_catalog(cat, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, on_triggered));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout));

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(MINOR_PAD_CODE));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, p->trigger(UNKNOWN_CODE));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(0, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(0, g_lockout_count);

    alarm_bridge_drain();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(0, g_lockout_count);
}

/* -------------------------------------------------------------------------
 * TC-11 故障组合
 * 攻击目标: 会话结束后 journal 仍可读且丢弃计数冻结，新 MAJOR 不得追加
 * 攻击手法: 灌满 journal 后 overflow 一次，结束会话再 trigger 新 MAJOR
 * ------------------------------------------------------------------------- */
static void test_journal_frozen_after_session_end(void)
{
    alarm_def_t cat[ALARM_SESSION_JOURNAL_MAX + 2U];
    uint32_t    codes[ALARM_SESSION_JOURNAL_MAX];
    uint32_t    dropped = 0U;
    uint32_t    extra   = fill_code(ALARM_SESSION_JOURNAL_MAX);
    uint32_t    after   = fill_code(ALARM_SESSION_JOURNAL_MAX + 1U);
    unsigned    i;
    unsigned    n;

    for (i = 0U; i < (ALARM_SESSION_JOURNAL_MAX + 2U); ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MAJOR);
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_SESSION_JOURNAL_MAX + 2U));
    alarm_registry_on_wash_session_started();

    for (i = 0U; i < ALARM_SESSION_JOURNAL_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(extra));

    n = alarm_registry_get_session_journal(codes, ALARM_SESSION_JOURNAL_MAX, &dropped);
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX, n);
    TEST_ASSERT_EQUAL_UINT32(1U, dropped);

    alarm_registry_on_wash_session_ended();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(after));

    n = alarm_registry_get_session_journal(codes, ALARM_SESSION_JOURNAL_MAX, &dropped);
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX, n);
    TEST_ASSERT_EQUAL_UINT32(1U, dropped);
    TEST_ASSERT_TRUE(alarm_registry_is_active(after));
}

/* -------------------------------------------------------------------------
 * TC-12 边界值
 * 攻击目标: 非法码与 journal 空缓冲不得崩溃或清零丢弃计数
 * 攻击手法: code=0 / UINT32_MAX；get_session_journal(NULL,0) 与 max=1 截断
 * ------------------------------------------------------------------------- */
static void test_boundary_codes_and_journal_null_buffer(void)
{
    const alarm_binding_ops_t *p = ops();
    alarm_def_t                cat[2];
    uint32_t                   one[1];
    uint32_t                   dropped = 99U;
    unsigned                   n;

    cat[0] = make_fill_def(fill_code(0U), ALARM_LEVEL_MAJOR);
    cat[1] = make_fill_def(fill_code(1U), ALARM_LEVEL_MAJOR);
    TEST_ASSERT_EQUAL_INT(SW_OK, p->load_catalog(cat, 2U));

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, p->trigger(ALARM_CODE_NONE));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, p->trigger(UINT32_MAX));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, p->clear(UINT32_MAX));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());

    alarm_registry_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(SW_OK, p->trigger(fill_code(0U)));
    TEST_ASSERT_EQUAL_INT(SW_OK, p->trigger(fill_code(1U)));

    n = alarm_registry_get_session_journal(NULL, 0U, &dropped);
    TEST_ASSERT_EQUAL_UINT(0U, n);
    TEST_ASSERT_EQUAL_UINT32(0U, dropped);

    n = alarm_registry_get_session_journal(one, 1U, &dropped);
    TEST_ASSERT_EQUAL_UINT(1U, n);
    TEST_ASSERT_EQUAL_UINT32(fill_code(0U), one[0]);
    TEST_ASSERT_EQUAL_UINT32(0U, dropped);
}

/* -------------------------------------------------------------------------
 * TC-13 初始化依赖
 * 攻击目标: 未装目录或重复 init 清目录后 trigger 必须失败，不得进入活动表
 * 攻击手法: 跳过 load_catalog；装载后再 alarm_registry_init
 * ------------------------------------------------------------------------- */
static void test_trigger_before_catalog_and_after_reinit(void)
{
    const alarm_binding_ops_t *p = ops();
    alarm_def_t                cat[] = {make_fill_def(CRIT_CODE, ALARM_LEVEL_CRITICAL)};

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, p->trigger(CRIT_CODE));
    TEST_ASSERT_FALSE(alarm_registry_is_active(CRIT_CODE));
    TEST_ASSERT_EQUAL_UINT(0U, alarm_registry_catalog_count());

    TEST_ASSERT_EQUAL_INT(SW_OK, p->load_catalog(cat, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_UINT(0U, alarm_registry_catalog_count());
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, p->trigger(CRIT_CODE));
    TEST_ASSERT_FALSE(alarm_registry_is_active(CRIT_CODE));
}

/* -------------------------------------------------------------------------
 * TC-14 初始化依赖
 * 攻击目标: 未 alarm_bridge_init 时 binding CRITICAL 仍须能入队 LOCKOUT
 * 攻击手法: 只 init event_bus + registry + bind，不调 alarm_bridge_init
 * ------------------------------------------------------------------------- */
static void test_binding_lockout_without_alarm_bridge_init(void)
{
    const alarm_binding_ops_t *p = ops();
    alarm_def_t                cat[] = {make_fill_def(CRIT_CODE, ALARM_LEVEL_CRITICAL)};

    TEST_ASSERT_EQUAL_INT(SW_OK, p->load_catalog(cat, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, on_triggered));

    TEST_ASSERT_EQUAL_INT(SW_OK, p->trigger(CRIT_CODE));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_clear_last_critical_delays_nominal_event,
                 "ALRM-22",
                 "对抗：清除末条 CRITICAL 后 NOMINAL 事件可延迟");
    WDF_RUN_TEST(test_eviction_prefers_minor_over_older_major,
                 "ALRM-20",
                 "对抗：混排时驱逐最早 MINOR 而非更早 MAJOR");
    WDF_RUN_TEST(test_retrigger_evicted_minor_does_not_kick_lockout,
                 "ALRM-20",
                 "对抗：被逐 MINOR 立刻重报不得挤掉 lockout");
    WDF_RUN_TEST(test_lockout_enqueued_before_handlers_run,
                 "ALRM-22",
                 "对抗：立即 drain 只入队不在采集线程跑 handler");
    WDF_RUN_TEST(test_copy_safety_view_stable_during_lockout_eviction,
                 "ALRM-20",
                 "对抗：驱逐同时拷贝安全视图不越界且姿态自洽");
    WDF_RUN_TEST(test_binding_drain_and_periodic_drain_do_not_overlap,
                 "ALRM-19",
                 "对抗：binding drain 与周期 drain 临界区不重叠");
    WDF_RUN_TEST(test_eviction_when_pending_full_still_admits_lockout,
                 "ALRM-13",
                 "对抗：pending 满时驱逐仍准入 lockout 且 RESYNC 或 CLEARED");
    WDF_RUN_TEST(test_journal_and_pool_full_then_critical,
                 "ALRM-21",
                 "对抗：journal 与活跃池同时满时 CRITICAL 仍准入");
    WDF_RUN_TEST(test_lockout_handler_retrigger_second_critical,
                 "ALRM-22",
                 "对抗：LOCKOUT 回调内再 trigger 不双发 LOCKOUT");
    WDF_RUN_TEST(test_unknown_trigger_does_not_drain_pending_minor,
                 "ALRM-22",
                 "对抗：未知码失败不得误 drain 已有 MINOR pending");
    WDF_RUN_TEST(test_journal_frozen_after_session_end,
                 "ALRM-21",
                 "对抗：会话结束后 journal 冻结且丢弃计数不清零");
    WDF_RUN_TEST(test_boundary_codes_and_journal_null_buffer,
                 "",
                 "对抗：非法码与 journal 空缓冲边界");
    WDF_RUN_TEST(test_trigger_before_catalog_and_after_reinit,
                 "",
                 "对抗：未装目录或重复 init 后 trigger 失败");
    WDF_RUN_TEST(test_binding_lockout_without_alarm_bridge_init,
                 "ALRM-22",
                 "对抗：未 alarm_bridge_init 仍能入队 LOCKOUT");

    return UNITY_END();
}
