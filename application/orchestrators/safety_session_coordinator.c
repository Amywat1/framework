/**
 * @file    safety_session_coordinator.c
 * @brief   安全会话协调实现
 *
 * @note    职责：
 *   EVT_HW_ESTOP_ON
 *     → safety_deferred_stop()
 *     → device_ops.abort_wash(WASH_ABORT_ESTOP)
 *     → 模式由 op_mode_bridge 切至 STOPPED（不跑 abort_home）
 *     → 离开 RECOVERING 时归位等待由 recovery_coordinator 自行取消
 *
 *   EVT_SAFETY_LOCKOUT
 *     → device_ops.abort_wash(WASH_ABORT_CRITICAL)
 *
 *   EVT_ABORT_HOME_REQUESTED
 *     → device_ops.abort_home()（仅启动；完成由项目发 EVT_ABORT_HOME_DONE）
 *
 *   不订阅 EVT_HW_ESTOP_OFF：清急停标志由 op_mode_bridge 独占。
 */

#include "application/orchestrators/safety_session_coordinator.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/ports/outbound/safety/safety_port.h"
#include "runtime/event_bus/event_bus.h"

static void abort_wash(wash_abort_cause_t cause)
{
    const device_ops_t *ops = device_ops_get();

    if ((ops != NULL) && (ops->abort_wash != NULL)) {
        ops->abort_wash(cause);
    }
}

static void on_estop_on(const event_t *evt)
{
    (void)evt;
    safety_deferred_stop();
    abort_wash(WASH_ABORT_ESTOP);
    LOG_WARN("safety_session: EVT_HW_ESTOP_ON, deferred stop + active session abort request");
}

static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    abort_wash(WASH_ABORT_CRITICAL);
    LOG_WARN("safety_session: LOCKOUT, active session abort request");
}

static void on_abort_home_requested(const event_t *evt)
{
    const device_ops_t *ops = device_ops_get();

    (void)evt;
    LOG_INFO("safety_session: abort_home requested");
    if ((ops != NULL) && (ops->abort_home != NULL)) {
        ops->abort_home();
    } else {
        LOG_ERROR("safety_session: ops.abort_home missing, publish DONE fail");
        (void)event_publish_required(EVT_ABORT_HOME_DONE, (uint32_t)SW_ERR_NOT_INIT);
    }
}

sw_err_t safety_session_coordinator_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_HW_ESTOP_ON,          on_estop_on            },
        {EVT_SAFETY_LOCKOUT,       on_safety_lockout      },
        {EVT_ABORT_HOME_REQUESTED, on_abort_home_requested},
    };

    return event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
}
