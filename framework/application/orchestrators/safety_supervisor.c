/**
 * @file    safety_supervisor.c
 * @brief   安全监督器实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/application/orchestrators/safety_supervisor.h"
#include "framework/domain/safety/alarm_registry/alarm_registry.h"
#include "framework/domain/safety/model/safety_types.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"

static void on_safety(const event_t *evt)
{
    switch (evt->type)
    {
    case EVT_SAFETY_LOCKOUT:
        dev_ctx_set_safety_posture(SAFETY_POSTURE_LOCKOUT);
        break;
    case EVT_SAFETY_NOMINAL:
    default:
        dev_ctx_set_safety_posture(SAFETY_POSTURE_NOMINAL);
        break;
    }
}

static void refresh_alarm_projection(const event_t *evt)
{
    alarm_instance_t list[ALARM_ACTIVE_MAX];
    bool             blocking = false;
    uint32_t         top      = ALARM_CODE_NONE;
    unsigned         count;

    (void)evt;

    count = alarm_registry_copy_active_projection(list,
                                                  ALARM_ACTIVE_MAX,
                                                  &blocking,
                                                  &top);
    dev_ctx_set_alarm_projection(blocking, top, list, count);
}

sw_err_t safety_supervisor_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_SAFETY_LOCKOUT,        on_safety },
        { EVT_SAFETY_NOMINAL,        on_safety },
        { EVT_ALARM_TRIGGERED,       refresh_alarm_projection },
        { EVT_ALARM_CLEARED,         refresh_alarm_projection },
        { EVT_ALARM_BATCH_CLEARED,   refresh_alarm_projection },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    refresh_alarm_projection(NULL);
    LOG_INFO("safety_supervisor: init ok");
    return SW_OK;
}
