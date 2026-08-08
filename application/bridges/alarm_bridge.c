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
#include "domain/device_control/model/actuator_events.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"

#include <sched.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * pending → EVT_ALARM_*，并边沿发布 EVT_SAFETY_*
 * ------------------------------------------------------------------------- */

static safety_posture_t s_posture = SAFETY_POSTURE_NOMINAL;

static void publish_posture_edge(void)
{
    safety_posture_t next = alarm_registry_safety_posture();

    if (next == s_posture) {
        return;
    }

    s_posture = next;
    if (next == SAFETY_POSTURE_LOCKOUT) {
        LOG_WARN("alarm_bridge: posture -> LOCKOUT");
        (void)event_publish(EVT_SAFETY_LOCKOUT, 0U);
    } else {
        LOG_INFO("alarm_bridge: posture -> NOMINAL");
        (void)event_publish(EVT_SAFETY_NOMINAL, 0U);
    }
}

void alarm_bridge_drain(void)
{
    alarm_domain_event_t batch[ALARM_PENDING_EVENT_MAX];
    unsigned             n;
    unsigned             i;
    bool                 any = false;

    do {
        n = alarm_registry_pull_events(batch, ALARM_PENDING_EVENT_MAX);
        for (i = 0U; i < n; ++i) {
            any = true;
            switch (batch[i].kind) {
            case ALARM_DOMAIN_EVT_TRIGGERED:
                (void)event_publish(EVT_ALARM_TRIGGERED, batch[i].code);
                break;
            case ALARM_DOMAIN_EVT_CLEARED:
                (void)event_publish(EVT_ALARM_CLEARED, batch[i].code);
                break;
            default:
                break;
            }
        }
    } while (n == ALARM_PENDING_EVENT_MAX);

    if (any) {
        publish_posture_edge();
    }
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

    s_posture = SAFETY_POSTURE_NOMINAL;

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
