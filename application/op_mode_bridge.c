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
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "ports/outbound/safety/op_mode_alarm_port.h"
#include "runtime/event_bus/event_bus.h"

static void on_wash_session_started(const event_t *evt)
{
    (void)evt;
    op_mode_on_wash_session_started();
}

static void on_wash_done(const event_t *evt)
{
    (void)evt;
    /* WASHING → WASH_DONE；客户离场后再由 EVT_WASH_CUSTOMER_GONE 触发 → IDLE */
    op_mode_on_wash_session_completed();
}

static void on_wash_aborted(const event_t *evt)
{
    wash_abort_cause_t cause = wash_abort_from_evt_param(evt->param);

    /* 急停路径：on_estop(true) 已将模式切换至 EXCEPTION，
     * on_wash_session_aborted 内部会检测到非 WASHING 态并提前返回 */
    op_mode_on_wash_session_aborted(cause);
}

static void on_wash_customer_gone(const event_t *evt)
{
    (void)evt;
    op_mode_on_wash_customer_gone();
}

static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    /* WASHING 时由 emergency_handler 发起中止，不在此立即切换模式；
     * 其余状态立即进入 EXCEPTION */
    op_mode_on_critical_alarm();
}

static void on_alarm_triggered(const event_t *evt)
{
    if (op_mode_alarm_port_is_estop(evt->param)) {
        op_mode_on_estop(true);
    }
}

static void on_alarm_cleared(const event_t *evt)
{
    if (op_mode_alarm_port_is_estop(evt->param)) {
        op_mode_on_estop(false);
    }
}

static void on_hw_estop_on(const event_t *evt)
{
    (void)evt;
    op_mode_on_estop(true);
}

static void on_hw_estop_off(const event_t *evt)
{
    (void)evt;
    op_mode_on_estop(false);
}

static void on_recovery_completed(const event_t *evt)
{
    op_mode_on_recovery_completed((recovery_result_t)evt->param);
}

static void on_self_check_completed(const event_t *evt)
{
    op_mode_on_self_check_completed(evt->param != 0U);
}

static void on_home_completed(const event_t *evt)
{
    /* param=1 表示归位成功（HOMING → IDLE），param=0 表示失败（HOMING → EXCEPTION）*/
    op_mode_on_home_done(evt->param != 0U);
}

static void on_alarm_home_done(const event_t *evt)
{
    /* EVT_SAFETY_HOME_DONE：安全归位完成（ALARM_HOMING → EXCEPTION）*/
    (void)evt;
    op_mode_on_home_done(true);
}

sw_err_t op_mode_bridge_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_WASH_SESSION_STARTED,         on_wash_session_started},
        {EVT_WASH_DONE,                    on_wash_done           },
        {EVT_WASH_ABORTED,                 on_wash_aborted        },
        {EVT_WASH_CUSTOMER_GONE,           on_wash_customer_gone  },
        {EVT_SAFETY_LOCKOUT,               on_safety_lockout      },
        {EVT_ALARM_TRIGGERED,              on_alarm_triggered     },
        {EVT_ALARM_CLEARED,                on_alarm_cleared       },
        {EVT_HW_ESTOP_ON,                  on_hw_estop_on         },
        {EVT_HW_ESTOP_OFF,                 on_hw_estop_off        },
        {EVT_OP_MODE_RECOVERY_COMPLETED,   on_recovery_completed  },
        {EVT_OP_MODE_SELF_CHECK_COMPLETED, on_self_check_completed},
        {EVT_OP_MODE_HOME_COMPLETED,       on_home_completed      },
        {EVT_SAFETY_HOME_DONE,             on_alarm_home_done     },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("op_mode_bridge: init ok");
    return SW_OK;
}
