/**
 * @file    safety_fsm.c
 * @brief   安全状态机实现
 * @author  HUWANGWEI
 * @date    2026-06-26
 */

#include "framework/domain/safety/safety_fsm/safety_fsm.h"
#include "framework/domain/safety/alarm/alarm_core.h"
#include "framework/domain/safety/model/safety_types.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"

/* 仅在 event_dispatch 线程访问，无需加锁 */
static safety_state_t s_state = SAFETY_STATE_OK;

/* EVT_ALARM_TRIGGERED / EVT_ALARM_CLEARED：重算聚合安全态，跃迁时广播 */
static void on_alarm_changed(const event_t *evt)
{
    (void)evt;

    safety_state_t next = alarm_core_safety_state();
    if (next == s_state)
    {
        return;
    }
    s_state = next;

    switch (next)
    {
    case SAFETY_STATE_LOCKOUT:
        LOG_WARN("safety_fsm: → LOCKOUT");
        (void)event_publish(EVT_SAFETY_LOCKOUT, 0U);
        break;
    case SAFETY_STATE_WARNING:
        LOG_WARN("safety_fsm: → WARNING");
        (void)event_publish(EVT_SAFETY_WARNING, 0U);
        break;
    case SAFETY_STATE_OK:
    default:
        LOG_INFO("safety_fsm: → OK");
        (void)event_publish(EVT_SAFETY_CLEARED, 0U);
        break;
    }
}

sw_err_t safety_fsm_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_ALARM_TRIGGERED, on_alarm_changed },
        { EVT_ALARM_CLEARED,   on_alarm_changed },
    };
    sw_err_t ret;

    s_state = SAFETY_STATE_OK;

    ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("safety_fsm: init ok (OK)");
    return SW_OK;
}
