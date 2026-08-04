/**
 * @file    safety_cutout_coordinator.c
 * @brief   安全切断协调实现
 * @author  HUWANGWEI
 * @date    2026-07-19
 *
 * @note    职责：
 *
 *   EVT_HW_ESTOP_ON
 *     → safety_deferred_stop()
 *     → machine_ops.abort_wash(WASH_ABORT_ESTOP)
 *     → 模式由 op_mode_bridge 切至 EXCEPTION（不跑 abort_home）
 *
 *   EVT_HW_ESTOP_OFF
 *     → 仅日志；清急停标志由 op_mode_bridge 处理
 *
 *   EVT_SAFETY_LOCKOUT
 *     → machine_ops.abort_wash(WASH_ABORT_CRITICAL)
 *     → 不在此立即 deferred_stop；停输出与 ABORT_HOMING 由会话结束路径承接
 */

#include "application/orchestrators/safety_cutout_coordinator.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "ports/outbound/safety/safety_port.h"
#include "runtime/event_bus/event_bus.h"

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
    LOG_WARN("safety_cutout: EVT_HW_ESTOP_ON, deferred stop + active session abort request");
}

static void on_estop_off(const event_t *evt)
{
    (void)evt;
    LOG_INFO("safety_cutout: EVT_HW_ESTOP_OFF");
}

static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    abort_wash(WASH_ABORT_CRITICAL);
    LOG_WARN("safety_cutout: LOCKOUT, active session abort request");
}

sw_err_t safety_cutout_coordinator_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_HW_ESTOP_ON,    on_estop_on      },
        {EVT_HW_ESTOP_OFF,   on_estop_off     },
        {EVT_SAFETY_LOCKOUT, on_safety_lockout},
    };

    return event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
}
