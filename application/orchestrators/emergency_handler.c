/**
 * @file    emergency_handler.c
 * @brief   安全紧急处理协调实现
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    职责：
 *
 *   EVT_HW_ESTOP_ON
 *     → safety_deferred_stop()（停止所有机构）
 *     → machine_ops.abort_wash(WASH_ABORT_ESTOP)
 *     → op_mode 由 op_mode_bridge 消费 EVT_HW_ESTOP_ON 切换至 EXCEPTION
 *
 *   EVT_HW_ESTOP_OFF
 *     → 仅清除急停标志（op_mode_bridge 消费），不执行自动归位
 *
 *   EVT_SAFETY_LOCKOUT
 *     → machine_ops.abort_wash(WASH_ABORT_CRITICAL)
 *
 *   EVT_OP_MODE_ALARM_HOME_REQUESTED
 *     → do_safety_home() → EVT_SAFETY_HOME_DONE
 */

#include "application/orchestrators/emergency_handler.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "ports/outbound/safety/safety_deferred_stop.h"
#include "runtime/event_bus/event_bus.h"

static void do_safety_home(void)
{
    const machine_ops_t *ops = machine_ops_get();

    if ((ops != NULL) && (ops->safety_home != NULL)) {
        ops->safety_home();
    }
    (void)event_publish(EVT_SAFETY_HOME_DONE, (uint32_t)SW_OK);
    LOG_INFO("emergency_handler: safety home done");
}

static void abort_wash(wash_abort_cause_t cause)
{
    const machine_ops_t *ops = machine_ops_get();

    if ((ops != NULL) && (ops->abort_wash != NULL)) {
        ops->abort_wash(cause);
    }
}

static void on_estop_on(const event_t *evt)
{
    (void)evt;
    safety_deferred_stop();
    abort_wash(WASH_ABORT_ESTOP);
    LOG_WARN("emergency_handler: EVT_HW_ESTOP_ON, all actuators stopped");
}

static void on_estop_off(const event_t *evt)
{
    (void)evt;
    LOG_INFO("emergency_handler: EVT_HW_ESTOP_OFF, estop cleared");
}

static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    abort_wash(WASH_ABORT_CRITICAL);
    LOG_WARN("emergency_handler: LOCKOUT, wash aborted");
}

static void on_alarm_home_requested(const event_t *evt)
{
    (void)evt;
    LOG_INFO("emergency_handler: alarm home requested, executing safety home");
    do_safety_home();
}

sw_err_t emergency_handler_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_HW_ESTOP_ON,                  on_estop_on             },
        {EVT_HW_ESTOP_OFF,                 on_estop_off            },
        {EVT_SAFETY_LOCKOUT,               on_safety_lockout       },
        {EVT_OP_MODE_ALARM_HOME_REQUESTED, on_alarm_home_requested },
    };

    return event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
}
