/**
 * @file    safety_supervisor.c
 * @brief   安全监督器实现
 * @author  HUWANGWEI
 * @date    2026-06-26
 */

#include "application/orchestrators/safety_supervisor.h"
#include "domain/safety/alarm_core.h"
#include "domain/model/safety_types.h"
#include "domain/model/alarm_code.h"
#include "service/dev_ctx/dev_ctx.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"

/* EVT_SAFETY_*：安全态投影到 dev_ctx */
static void on_safety(const event_t *evt)
{
    safety_state_t state;

    switch (evt->type)
    {
    case EVT_SAFETY_LOCKOUT:
        state = SAFETY_STATE_LOCKOUT;
        break;
    case EVT_SAFETY_WARNING:
        state = SAFETY_STATE_WARNING;
        break;
    case EVT_SAFETY_CLEARED:
    default:
        state = SAFETY_STATE_OK;
        break;
    }

    dev_ctx_set_safety_state(state);
}

/* EVT_ALARM_*：当前最高等级报警投影到 dev_ctx（向 alarm_core 取权威活跃集）*/
static void on_alarm(const event_t *evt)
{
    (void)evt;

    uint32_t top = alarm_core_top_code();
    dev_ctx_set_alarm_state(top != ALARM_CODE_NONE, top);
}

sw_err_t safety_supervisor_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_SAFETY_LOCKOUT,  on_safety },
        { EVT_SAFETY_WARNING,  on_safety },
        { EVT_SAFETY_CLEARED,  on_safety },
        { EVT_ALARM_TRIGGERED, on_alarm  },
        { EVT_ALARM_CLEARED,   on_alarm  },
    };
    sw_err_t ret;

    ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("safety_supervisor: init ok");
    return SW_OK;
}
