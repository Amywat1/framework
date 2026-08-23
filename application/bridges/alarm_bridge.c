/**
 * @file    alarm_bridge.c
 * @brief   报警域应用桥接实现
 * @author  HUWANGWEI
 * @date    2026-08-08
 */

#include "application/bridges/alarm_bridge.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/mechanism/model/actuator_events.h"
#include "domain/wash/wash_events.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"

#include <pthread.h>
#include <sched.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * pending → EVT_ALARM_*，并边沿发布 EVT_SAFETY_*
 * ------------------------------------------------------------------------- */

/* drain 串行化锁。
 *
 * 为何需要：`alarm_bridge_drain` 既由 50ms 周期任务调用，也作为导出接口供项目
 * wiring 与 demo 做同步验证（`demo/app/demo_main.c` 就在主线程调它，而周期任务
 * 同时在跑）。逐码事件本身安全——`pull_events` 自身持锁，事件不会重复取出；
 * 出问题的是 `s_posture` 的读-比较-改写：两个线程并发时可能重复发布
 * EVT_SAFETY_LOCKOUT，或各自看到"没变化"而双双漏发边沿。
 *
 * 为何锁整个 drain 而不只锁边沿判定：只锁判定的话，两个线程可以以相反顺序发布
 * LOCKOUT/NOMINAL。对安全事件而言顺序倒置比多发一次更糟。整体串行化后边沿判定
 * 天然只有一个赢家。
 *
 * 锁序：bridge → event_bus。没有任何事件 handler 回调 drain，不成环。
 * event_publish 是非阻塞入队，持锁跨发布的代价有界。 */
static pthread_mutex_t s_drain_mutex = PTHREAD_MUTEX_INITIALIZER;

static safety_posture_t s_posture = SAFETY_POSTURE_NOMINAL;

#ifdef ALARM_BRIDGE_UNIT_TEST
/* 临界区重叠计数。与 drain 锁分开：有 drain 锁时深度恒为 1；去掉 drain 锁后
 * 停留钩子把窗口拉到可观测，重叠计数非 0。这是 ALRM-19 能稳定失败的观测点。 */
static int             s_cs_depth;
static int             s_cs_overlap;
static pthread_mutex_t s_cs_stat_mutex = PTHREAD_MUTEX_INITIALIZER;
static void (*s_in_cs_hook)(void);

static void cs_enter(void)
{
    pthread_mutex_lock(&s_cs_stat_mutex);
    if (s_cs_depth != 0) {
        s_cs_overlap++;
    }
    s_cs_depth++;
    pthread_mutex_unlock(&s_cs_stat_mutex);
    if (s_in_cs_hook != NULL) {
        s_in_cs_hook();
    }
}

static void cs_leave(void)
{
    pthread_mutex_lock(&s_cs_stat_mutex);
    if (s_cs_depth > 0) {
        s_cs_depth--;
    }
    pthread_mutex_unlock(&s_cs_stat_mutex);
}

void alarm_bridge_reset_for_test(void)
{
    pthread_mutex_lock(&s_drain_mutex);
    s_posture = SAFETY_POSTURE_NOMINAL;
    pthread_mutex_unlock(&s_drain_mutex);

    pthread_mutex_lock(&s_cs_stat_mutex);
    s_cs_depth   = 0;
    s_cs_overlap = 0;
    pthread_mutex_unlock(&s_cs_stat_mutex);
    s_in_cs_hook = NULL;
}

void alarm_bridge_test_set_in_cs_hook(void (*fn)(void))
{
    s_in_cs_hook = fn;
}

int alarm_bridge_test_cs_overlap(void)
{
    int overlap;

    pthread_mutex_lock(&s_cs_stat_mutex);
    overlap = s_cs_overlap;
    pthread_mutex_unlock(&s_cs_stat_mutex);
    return overlap;
}
#endif

/** @note 调用方必须已持 s_drain_mutex。 */
static void publish_posture_edge_locked(void)
{
    safety_posture_t next = alarm_registry_safety_posture();

    if (next == s_posture) {
        return;
    }

    s_posture = next;
    if (next == SAFETY_POSTURE_LOCKOUT) {
        LOG_WARN("alarm_bridge: posture -> LOCKOUT");
        (void)event_publish_required(EVT_SAFETY_LOCKOUT, 0U);
    } else {
        sw_err_t ret;

        LOG_INFO("alarm_bridge: posture -> NOMINAL");
        ret = event_publish(EVT_SAFETY_NOMINAL, 0U);
        if (ret != SW_OK) {
            LOG_WARN("alarm_bridge: nominal projection event dropped ret=%d", (int)ret);
        }
    }
}

/* 单次 drain 的最大轮次。registry 的待发队列可以被上游持续写入，无上界地
 * 排空会让 50ms 周期任务在报警抖动时长时间不返回。超出的事件留到下一拍，
 * 不会丢——队列自身满了才丢，那条路径由 dropped 计数负责。 */
#define ALARM_DRAIN_MAX_ROUNDS 4U

void alarm_bridge_drain(void)
{
    alarm_domain_event_t batch[ALARM_PENDING_EVENT_MAX];
    unsigned             n;
    unsigned             i;
    unsigned             rounds       = 0U;
    uint32_t             dropped      = 0U;
    uint32_t             dropped_seen = 0U;
    bool                 any          = false;

    pthread_mutex_lock(&s_drain_mutex);
#ifdef ALARM_BRIDGE_UNIT_TEST
    cs_enter();
#endif

    do {
        n = alarm_registry_pull_events(batch, ALARM_PENDING_EVENT_MAX, &dropped);
        dropped_seen += dropped;
        for (i = 0U; i < n; ++i) {
            any = true;
            switch (batch[i].kind) {
            case ALARM_DOMAIN_EVT_TRIGGERED:
                (void)event_publish_required(EVT_ALARM_TRIGGERED, batch[i].code);
                break;
            case ALARM_DOMAIN_EVT_CLEARED:
                (void)event_publish_required(EVT_ALARM_CLEARED, batch[i].code);
                break;
            default:
                break;
            }
        }
        rounds++;
    } while ((n == ALARM_PENDING_EVENT_MAX) && (rounds < ALARM_DRAIN_MAX_ROUNDS));

    /* 有事件被丢：逐码事件已经不完整，凡是靠 EVT_ALARM_* 边沿维护派生状态的
     * 订阅者都必须重读 registry。发在逐码事件之后，保证重读看到的是最终状态。 */
    if (dropped_seen > 0U) {
        LOG_ERROR("alarm_bridge: %u domain events dropped, publishing resync", (unsigned)dropped_seen);
        (void)event_publish_required(EVT_ALARM_RESYNC, dropped_seen);
    }

    /* 丢弃也算"有变更"：单消费者下队列满必然伴随 n==MAX，但多消费者时另一个线程
     * 可能刚把队列取空、只留下丢弃计数给本次调用，此时 any 为假却确实漏过事件。
     * 姿态边沿是按 registry 当前状态重算的，多算一次没有副作用（相同则直接返回）。 */
    if (any || (dropped_seen > 0U)) {
        publish_posture_edge_locked();
    }

#ifdef ALARM_BRIDGE_UNIT_TEST
    cs_leave();
#endif
    pthread_mutex_unlock(&s_drain_mutex);
}

static void bridge_tick(void *ctx)
{
    (void)ctx;
    alarm_bridge_drain();
}

/* -------------------------------------------------------------------------
 * 洗车会话生命周期 → alarm_registry
 * ------------------------------------------------------------------------- */

static void on_wash_session_started(const event_t *evt)
{
    (void)evt;
    alarm_registry_on_wash_session_started();
}

static void on_wash_session_ended(const event_t *evt)
{
    (void)evt;
    alarm_registry_on_wash_session_ended();
}

/* -------------------------------------------------------------------------
 * ON_MOTION 重评估（项目按需 init）
 * ------------------------------------------------------------------------- */

static const alarm_reeval_binding_t *s_bindings;
static size_t                        s_binding_count;

static bool trigger_kind_valid(alarm_reeval_trigger_kind_t kind)
{
    return (kind == ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED) || (kind == ALARM_REEVAL_TRIGGER_WASH_CHECKPOINT);
}

static bool binding_table_valid(const alarm_reeval_binding_t *bindings, size_t count)
{
    size_t i;
    size_t j;

    if (count > ALARM_REEVAL_BINDING_MAX) {
        return false;
    }
    if ((count > 0U) && (bindings == NULL)) {
        return false;
    }

    for (i = 0U; i < count; ++i) {
        if (!trigger_kind_valid(bindings[i].kind) || (bindings[i].trigger_id == 0U)
            || (bindings[i].group == ALARM_REEVAL_GROUP_NONE)) {
            return false;
        }
        for (j = i + 1U; j < count; ++j) {
            if ((bindings[i].kind == bindings[j].kind) && (bindings[i].trigger_id == bindings[j].trigger_id)) {
                return false;
            }
        }
    }

    return true;
}

sw_err_t alarm_bridge_reeval_handle(alarm_reeval_trigger_kind_t kind, uint16_t trigger_id)
{
    size_t i;

    if (!trigger_kind_valid(kind) || (trigger_id == 0U)) {
        return SW_ERR_PARAM;
    }

    for (i = 0U; i < s_binding_count; ++i) {
        const alarm_reeval_binding_t *binding = &s_bindings[i];

        if ((binding->kind == kind) && (binding->trigger_id == trigger_id)) {
            return alarm_registry_reevaluate_group(binding->group);
        }
    }

    return SW_OK;
}

static void on_motion_completed(const event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    (void)alarm_bridge_reeval_handle(ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED, actuator_motion_completed_id(evt));
}

static void on_checkpoint_reached(const event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    (void)alarm_bridge_reeval_handle(ALARM_REEVAL_TRIGGER_WASH_CHECKPOINT, wash_checkpoint_reached_id(evt));
}

sw_err_t alarm_bridge_reeval_init(const alarm_reeval_binding_t *bindings, size_t count)
{
    static const event_subscription_t subs[] = {
        {EVT_COMP_MOTION_COMPLETED,   on_motion_completed  },
        {EVT_WASH_CHECKPOINT_REACHED, on_checkpoint_reached},
    };
    sw_err_t ret;

    if (!binding_table_valid(bindings, count)) {
        return SW_ERR_PARAM;
    }

    ret = event_subscribe_table(subs, sizeof(subs) / sizeof(subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    s_bindings      = bindings;
    s_binding_count = count;
    return SW_OK;
}

sw_err_t alarm_bridge_init(void)
{
    static const event_subscription_t s_life_subs[] = {
        {EVT_WASH_SESSION_STARTED, on_wash_session_started},
        {EVT_WASH_DONE,            on_wash_session_ended  },
        {EVT_WASH_ABORTED,         on_wash_session_ended  },
    };
    sw_err_t ret;

    pthread_mutex_lock(&s_drain_mutex);
    s_posture = SAFETY_POSTURE_NOMINAL;
    pthread_mutex_unlock(&s_drain_mutex);

    ret = periodic_task_register("alarm_bridge", 50U, bridge_tick, NULL, SCHED_OTHER, 0, THD_SENSOR_POLL_STACK);
    if (ret != SW_OK) {
        return ret;
    }

    ret = event_subscribe_table(s_life_subs, sizeof(s_life_subs) / sizeof(s_life_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("alarm_bridge: init ok");
    return SW_OK;
}
