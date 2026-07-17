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
 *     → wash_orchestrator_abort(WASH_ABORT_ESTOP)（通知洗车引擎中止）
 *     → op_mode 由 op_mode_bridge 消费 EVT_HW_ESTOP_ON 切换至 EXCEPTION
 *
 *   EVT_HW_ESTOP_OFF
 *     → 仅清除急停标志（op_mode_bridge 消费），不执行自动归位
 *     → 工作人员应通过手动动作或 RECOVER 指令进行后续操作
 *
 *   EVT_SAFETY_LOCKOUT（非急停路径触发 CRITICAL 报警）
 *     → wash_orchestrator_abort(WASH_ABORT_CRITICAL)（中止洗车）
 *     → op_mode 内部通过 EVT_WASH_ABORTED 进入 ALARM_HOMING，
 *       再发布 EVT_OP_MODE_ALARM_HOME_REQUESTED
 *
 *   EVT_OP_MODE_ALARM_HOME_REQUESTED
 *     → do_safety_home()（报警归位：刷子等回零，龙门不动）
 *     → 发布 EVT_SAFETY_HOME_DONE，op_mode_bridge 消费后 ALARM_HOMING → EXCEPTION
 */

#include "application/orchestrators/emergency_handler.h"

#include "application/orchestrators/wash_orchestrator.h"
#include "ports/outbound/safety/safety_deferred_stop.h"
#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "ports/outbound/machine/machine_ops_port.h"
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

static void on_estop_on(const event_t *evt)
{
    (void)evt;
    safety_deferred_stop();
    /* 通知洗车引擎中止（原因 ESTOP），引擎发布 EVT_WASH_ABORTED，
     * op_mode_bridge 收到后发现模式已是 EXCEPTION（由 on_hw_estop_on 切换），
     * on_wash_session_aborted 会提前返回 */
    wash_orchestrator_abort(WASH_ABORT_ESTOP);
    LOG_WARN("emergency_handler: EVT_HW_ESTOP_ON, all actuators stopped");
}

static void on_estop_off(const event_t *evt)
{
    (void)evt;
    /* 急停释放后不自动归位：由工作人员手动操作或执行 RECOVER 指令 */
    LOG_INFO("emergency_handler: EVT_HW_ESTOP_OFF, estop cleared");
}

static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    /* 中止洗车；归位由 ALARM_HOMING 路径处理（EVT_OP_MODE_ALARM_HOME_REQUESTED）*/
    wash_orchestrator_abort(WASH_ABORT_CRITICAL);
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
