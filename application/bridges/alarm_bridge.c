/**
 * @file    alarm_bridge.c
 * @brief   报警应用桥接：入站端口、会话生命周期、可选 ON_MOTION 重评估
 * @author  HUWANGWEI
 * @date    2026-08-08
 */

#include "application/bridges/alarm_bridge.h"

#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/mechanism/model/actuator_events.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "domain/wash/wash_events.h"
#include "runtime/event_bus/event_bus.h"

#include <stddef.h>

sw_err_t alarm_bridge_bind(void)
{
    static const alarm_binding_ops_t s_ops = {
        .trigger      = alarm_registry_trigger,
        .clear        = alarm_registry_clear,
        .load_catalog = alarm_registry_load_catalog,
    };

    sw_err_t ret = alarm_binding_register(&s_ops);
    if (ret != SW_OK) {
        LOG_ERROR("alarm_bridge: register binding ret=%d", (int)ret);
        return ret;
    }
    LOG_INFO("alarm_bridge: bound to alarm_registry");
    return SW_OK;
}

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

    ret = event_subscribe_table(s_life_subs, sizeof(s_life_subs) / sizeof(s_life_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("alarm_bridge: init ok");
    return SW_OK;
}
