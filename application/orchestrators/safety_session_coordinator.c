/**
 * @file    safety_session_coordinator.c
 * @brief   安全会话协调实现
 *
 * @note    职责：
 *   EVT_HW_ESTOP_ON
 *     → safety_deferred_stop()
 *     → device_ops.abort_jobs(WASH_ABORT_ESTOP)
 *     → 模式由 op_mode_bridge 切至 STOPPED
 *
 *   EVT_ALARM_TRIGGERED（目录等级 CRITICAL，急停码除外）
 *     → device_ops.abort_jobs(WASH_ABORT_CRITICAL)
 *       急停码走 EVT_HW_ESTOP_ON，不按 CRITICAL 清障
 *       LOCKOUT 只表示不能接下辆；停当前运动只看 CRITICAL 新进表
 *
 *   EVT_CLEAR_MACHINE_REQUESTED
 *     → 启动 device_ops.clear_machine()（完成由项目发 EVT_CLEAR_MACHINE_DONE）
 *       洗车中的 CRITICAL 也走清障；清障中再来 CRITICAL 由 abort_jobs 停会话
 */

#include "application/orchestrators/safety_session_coordinator.h"

#include "application/job_admit.h"
#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/ports/outbound/safety/safety_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "runtime/event_bus/event_bus.h"

static void abort_jobs(wash_abort_cause_t cause)
{
    const device_ops_t *ops = device_ops_get();

    if ((ops != NULL) && (ops->abort_jobs != NULL)) {
        ops->abort_jobs(cause);
    }
    job_admit_on_aborted(cause);
}

static void on_estop_on(const event_t *evt)
{
    (void)evt;
    safety_deferred_stop();
    abort_jobs(WASH_ABORT_ESTOP);
    LOG_WARN("safety_session: EVT_HW_ESTOP_ON, deferred stop + abort jobs");
}

/**
 * @brief  CRITICAL 新进表：停当前方案会话，不要求 LOCKOUT 边沿
 * @note   急停码由 EVT_HW_ESTOP_ON 独占，避免再走 CRITICAL 清障。
 */
static void on_alarm_triggered(const event_t *evt)
{
    if ((evt == NULL) || !alarm_registry_code_is_critical(evt->param)) {
        return;
    }
    if (op_mode_alarm_port_is_estop(evt->param)) {
        return;
    }
    abort_jobs(WASH_ABORT_CRITICAL);
    LOG_WARN("safety_session: critical %06u, abort jobs", (unsigned)evt->param);
}

static void on_clear_requested(const event_t *evt)
{
    const device_ops_t *ops = device_ops_get();

    (void)evt;
    LOG_INFO("safety_session: clear_machine requested");
    if ((ops != NULL) && (ops->clear_machine != NULL)) {
        ops->clear_machine();
    } else {
        LOG_ERROR("safety_session: ops.clear_machine missing, publish DONE fail");
        (void)event_publish_required(EVT_CLEAR_MACHINE_DONE, (uint32_t)SW_ERR_NOT_INIT);
    }
}

sw_err_t safety_session_coordinator_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_HW_ESTOP_ON,             on_estop_on        },
        {EVT_ALARM_TRIGGERED,         on_alarm_triggered },
        {EVT_CLEAR_MACHINE_REQUESTED, on_clear_requested },
    };

    return event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
}
