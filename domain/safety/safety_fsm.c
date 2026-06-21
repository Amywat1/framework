/**
 * @file    safety_fsm.c
 * @brief   安全状态机实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "domain/safety/safety_fsm.h"
#include "domain/safety/alarm_core.h"
#include "service/dev_ctx/dev_ctx.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"

/**
 * @brief  同步 ERROR 级报警标志到 dev_ctx
 */
static void sync_dev_ctx_alarm(void)
{
    dev_ctx_set_alarm_state(alarm_core_has_error());
}

/**
 * @brief  根据当前激活报警重新计算状态，更新 dev_ctx 并发布事件
 */
static void do_reevaluate(void)
{
    safety_state_t new_state;
    safety_state_t old_state;

    if (alarm_core_has_error())
    {
        new_state = SAFETY_STATE_LOCKOUT;
    }
    else if (alarm_core_has_warning())
    {
        new_state = SAFETY_STATE_WARNING;
    }
    else
    {
        new_state = SAFETY_STATE_OK;
    }

    sync_dev_ctx_alarm();

    old_state = dev_ctx_get_safety_state();
    if (new_state == old_state)
    {
        return;
    }

    dev_ctx_set_safety_state(new_state);

    if (new_state == SAFETY_STATE_LOCKOUT)
    {
        (void)event_publish(EVT_SAFETY_LOCKOUT, 0U);
        LOG_ERROR("safety_fsm: → LOCKOUT");
    }
    else if (new_state == SAFETY_STATE_WARNING)
    {
        (void)event_publish(EVT_SAFETY_WARNING, 0U);
        LOG_WARN("safety_fsm: → WARNING");
    }
    else
    {
        (void)event_publish(EVT_SAFETY_CLEARED, 0U);
        LOG_INFO("safety_fsm: → OK (CLEARED)");
    }
}

static void on_alarm_triggered(const event_t *evt)
{
    (void)evt;
    do_reevaluate();
}

static void on_alarm_cleared(const event_t *evt)
{
    (void)evt;
    do_reevaluate();
}

sw_err_t safety_fsm_init(void)
{
    static const event_subscription_t s_alarm_subs[] = {
        { EVT_ALARM_TRIGGERED, on_alarm_triggered },
        { EVT_ALARM_CLEARED,   on_alarm_cleared   },
    };
    sw_err_t ret;

    dev_ctx_set_safety_state(SAFETY_STATE_OK);
    sync_dev_ctx_alarm();

    ret = event_subscribe_table(s_alarm_subs,
                                sizeof(s_alarm_subs) / sizeof(s_alarm_subs[0]));
    if (ret != SW_OK)
    {
        LOG_ERROR("safety_fsm_init: subscribe failed");
        return ret;
    }

    LOG_INFO("safety_fsm: init ok");
    return SW_OK;
}

safety_state_t safety_fsm_get_state(void)
{
    return dev_ctx_get_safety_state();
}

void safety_fsm_reevaluate(void)
{
    do_reevaluate();
}
