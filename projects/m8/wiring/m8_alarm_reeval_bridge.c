/**
 * @file    m8_alarm_reeval_bridge.c
 * @brief   M8 机构/流程生命周期事件 → ON_MOTION 重评估桥接
 * @author  HUWANGWEI
 * @date    2026-07-10
 */

#include "projects/m8/wiring/m8_alarm_reeval_bridge.h"
#include "projects/m8/wiring/m8_alarm_reeval_bindings.h"
#include "framework/domain/safety/alarm_registry/alarm_registry.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include "framework/runtime/event_bus/event_bus.h"

static void on_motion_completed(const event_t *evt)
{
    motion_reeval_group_id_t group;

    if (evt == NULL)
    {
        return;
    }

    group = m8_reeval_group_lookup(REEVAL_TRIGGER_ACTUATOR, (uint16_t)evt->param);
    if (group != ALARM_REEVAL_GROUP_NONE)
    {
        (void)alarm_registry_reevaluate_group(group);
    }
}

static void on_checkpoint_reached(const event_t *evt)
{
    motion_reeval_group_id_t group;

    if (evt == NULL)
    {
        return;
    }

    group = m8_reeval_group_lookup(REEVAL_TRIGGER_PROCESS, (uint16_t)evt->param);
    if (group != ALARM_REEVAL_GROUP_NONE)
    {
        (void)alarm_registry_reevaluate_group(group);
    }
}

sw_err_t m8_alarm_reeval_bridge_init(void)
{
    sw_err_t r;

    r = event_subscribe(EVT_COMP_MOTION_COMPLETED, on_motion_completed);
    if (r != SW_OK)
    {
        LOG_ERROR("m8_alarm_reeval_bridge: subscribe motion_completed failed ret=%d", (int)r);
        return r;
    }

    r = event_subscribe(EVT_WASH_CHECKPOINT_REACHED, on_checkpoint_reached);
    if (r != SW_OK)
    {
        LOG_ERROR("m8_alarm_reeval_bridge: subscribe checkpoint_reached failed ret=%d", (int)r);
        return r;
    }

    LOG_INFO("m8_alarm_reeval_bridge: init ok");
    return SW_OK;
}
