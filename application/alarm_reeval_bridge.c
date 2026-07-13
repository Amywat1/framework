/**
 * @file    alarm_reeval_bridge.c
 * @brief   ON_MOTION 报警重评估桥接实现
 * @author  HUWANGWEI
 * @date    2026-07-12
 */

#include "application/alarm_reeval_bridge.h"

#include "common/event_types.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "runtime/event_bus/event_bus.h"

#include <stdbool.h>

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

sw_err_t alarm_reeval_bridge_handle(alarm_reeval_trigger_kind_t kind, uint16_t trigger_id)
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
    (void)alarm_reeval_bridge_handle(ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED, (uint16_t)evt->param);
}

static void on_checkpoint_reached(const event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    (void)alarm_reeval_bridge_handle(ALARM_REEVAL_TRIGGER_WASH_CHECKPOINT, (uint16_t)evt->param);
}

sw_err_t alarm_reeval_bridge_init(const alarm_reeval_binding_t *bindings, size_t count)
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
