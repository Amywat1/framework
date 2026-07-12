/**
 * @file    op_mode_bridge.c
 * @brief   运行模式事件桥接实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "application/op_mode_bridge.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/command_gateway/op_mode_types.h"
#include "domain/command_gateway/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "ports/outbound/safety/op_mode_alarm_port.h"
#include "runtime/event_bus/event_bus.h"

static void on_wash_session_started(const event_t *evt)
{
    (void)evt;
    alarm_registry_on_wash_session_started();
    op_mode_on_wash_session_started();
}

static void on_wash_done(const event_t *evt)
{
    bool enter_exception;

    (void)evt;
    op_mode_on_wash_session_completed();
    alarm_registry_on_wash_session_ended();
    enter_exception = alarm_registry_has_blocking_active() || op_mode_is_estop_active();
    op_mode_on_post_wash_assessment(enter_exception);
}

static void on_wash_aborted(const event_t *evt)
{
    wash_abort_cause_t cause = wash_abort_from_evt_param(evt->param);

    op_mode_on_wash_session_aborted(cause);
    alarm_registry_on_wash_session_ended();

    if (cause != WASH_ABORT_MANUAL) {
        bool enter_exception = alarm_registry_has_blocking_active() || op_mode_is_estop_active();
        op_mode_on_post_wash_assessment(enter_exception);
    }
}

static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    op_mode_on_critical_alarm();
}

static void on_alarm_triggered(const event_t *evt)
{
    if (op_mode_alarm_port_is_estop(evt->param)) {
        op_mode_on_estop_triggered();
    }
}

static void on_alarm_cleared(const event_t *evt)
{
    if (op_mode_alarm_port_is_estop(evt->param)) {
        op_mode_on_estop_cleared();
    }
}

static void on_hw_estop_on(const event_t *evt)
{
    (void)evt;
    op_mode_on_estop_triggered();
}

static void on_hw_estop_off(const event_t *evt)
{
    (void)evt;
    op_mode_on_estop_cleared();
}

static void on_recovery_completed(const event_t *evt)
{
    op_mode_on_recovery_completed((recovery_result_t)evt->param);
}

static void on_self_check_completed(const event_t *evt)
{
    op_mode_on_self_check_completed(evt->param != 0U);
}

sw_err_t op_mode_bridge_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_WASH_SESSION_STARTED,         on_wash_session_started},
        {EVT_WASH_DONE,                    on_wash_done           },
        {EVT_WASH_ABORTED,                 on_wash_aborted        },
        {EVT_SAFETY_LOCKOUT,               on_safety_lockout      },
        {EVT_ALARM_TRIGGERED,              on_alarm_triggered     },
        {EVT_ALARM_CLEARED,                on_alarm_cleared       },
        {EVT_HW_ESTOP_ON,                  on_hw_estop_on         },
        {EVT_HW_ESTOP_OFF,                 on_hw_estop_off        },
        {EVT_OP_MODE_RECOVERY_COMPLETED,   on_recovery_completed  },
        {EVT_OP_MODE_SELF_CHECK_COMPLETED, on_self_check_completed},
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("op_mode_bridge: init ok");
    return SW_OK;
}
