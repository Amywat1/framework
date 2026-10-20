/**
 * @file    recovery_coordinator.c
 * @brief   Recover 用例协调：安全触发门禁、清非安全报警、等清故障、异步归位
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "application/orchestrators/recovery_coordinator.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "runtime/event_bus/event_bus.h"

#include <stdatomic.h>
#include <stdbool.h>

typedef enum {
    RECOVERY_PHASE_IDLE = 0,
    RECOVERY_PHASE_WAIT_CLEAR,
    RECOVERY_PHASE_WAIT_HOME,
} recovery_phase_t;

static atomic_int s_phase;

/**
 * @brief  结束恢复并发布结果
 * @param  result 落地结果
 */
static void complete_recovery(recovery_result_t result)
{
    atomic_store(&s_phase, (int)RECOVERY_PHASE_IDLE);
    (void)event_publish_required(EVT_OP_MODE_RECOVERY_COMPLETED, (uint32_t)result);
    LOG_INFO("recovery_coordinator: completed result=%d", (int)result);
}

/**
 * @brief  取消进行中的清故障或归位等待
 */
static void cancel_pending(void)
{
    if ((recovery_phase_t)atomic_exchange(&s_phase, (int)RECOVERY_PHASE_IDLE) != RECOVERY_PHASE_IDLE) {
        LOG_WARN("recovery_coordinator: pending wait cancelled");
    }
}

/**
 * @brief  模式变更：离开 RECOVERING 即取消等待
 */
static void on_mode_changed(const event_t *evt)
{
    if (op_mode_changed_from(evt->param) == OP_MODE_RECOVERING) {
        cancel_pending();
    }
}

/**
 * @brief  安全触发仍成立则失败结束
 * @return true 已失败结束
 */
static bool abort_if_safety_trigger(void)
{
    if (!alarm_registry_has_safety_trigger_active()) {
        return false;
    }
    LOG_WARN("recovery_coordinator: safety trigger active, abort recovery");
    complete_recovery(RECOVERY_RESULT_FAILED);
    return true;
}

/**
 * @brief  认领相位 from→to
 * @return 本线程赢到认领为 true
 */
static bool claim_phase(recovery_phase_t from, recovery_phase_t to)
{
    int expected = (int)from;

    return atomic_compare_exchange_strong(&s_phase, &expected, (int)to);
}

/**
 * @brief  启动归位并等待 HOME_COMPLETED
 * @note   先认领 IDLE→WAIT_HOME，再核对仍是 RECOVERING。
 *         清故障结束先落到 IDLE，避免与 cancel 抢同一相位时误开跑。
 *         已离开则退回 IDLE、不开跑、不二次收尾。
 */
static void start_home(void)
{
    const device_ops_t *ops;

    if (!claim_phase(RECOVERY_PHASE_IDLE, RECOVERY_PHASE_WAIT_HOME)) {
        return;
    }
    if (op_mode_get_current() != OP_MODE_RECOVERING) {
        atomic_store(&s_phase, (int)RECOVERY_PHASE_IDLE);
        LOG_WARN("recovery_coordinator: not recovering, skip home");
        return;
    }
    if (abort_if_safety_trigger()) {
        return;
    }

    ops = device_ops_get();
    if ((ops == NULL) || (ops->home_device == NULL)) {
        LOG_ERROR("recovery_coordinator: home_device not available");
        complete_recovery(RECOVERY_RESULT_FAILED);
        return;
    }

    if (ops->home_device() != SW_OK) {
        atomic_store(&s_phase, (int)RECOVERY_PHASE_IDLE);
        LOG_ERROR("recovery_coordinator: home_device start failed");
        complete_recovery(RECOVERY_RESULT_FAILED);
        return;
    }

    LOG_INFO("recovery_coordinator: home started, waiting HOME_COMPLETED");
}

/**
 * @brief  清非安全报警后启动归位
 */
static void reset_and_home(void)
{
    if (op_mode_get_current() != OP_MODE_RECOVERING) {
        return;
    }
    alarm_registry_reset_non_safety();
    start_home();
}

/**
 * @brief  归位完成后按当时禁止接单报警落地
 */
static void on_home_completed(const event_t *evt)
{
    recovery_result_t result;
    bool              blocking_active;

    if (op_mode_get_current() != OP_MODE_RECOVERING) {
        cancel_pending();
        return;
    }

    if ((recovery_phase_t)atomic_exchange(&s_phase, (int)RECOVERY_PHASE_IDLE)
        != RECOVERY_PHASE_WAIT_HOME) {
        return;
    }

    if (evt->param == 0U) {
        LOG_ERROR("recovery_coordinator: home failed during recover");
        complete_recovery(RECOVERY_RESULT_FAILED);
        return;
    }

    blocking_active = alarm_registry_has_blocking_active();
    result          = blocking_active ? RECOVERY_RESULT_FAILED : RECOVERY_RESULT_STANDBY;
    if (blocking_active) {
        LOG_WARN("recovery_coordinator: blocking alarm remains after home");
    }
    complete_recovery(result);
}

static void on_recovery_requested(const event_t *evt)
{
    const device_ops_t *ops;

    (void)evt;
    atomic_store(&s_phase, (int)RECOVERY_PHASE_IDLE);

    if (abort_if_safety_trigger()) {
        return;
    }

    ops = device_ops_get();
    if ((ops != NULL) && (ops->clear_alarms != NULL)) {
        (void)ops->clear_alarms();
    }

    if ((ops != NULL) && (ops->poll_clear_alarms != NULL)) {
        atomic_store(&s_phase, (int)RECOVERY_PHASE_WAIT_CLEAR);
        LOG_INFO("recovery_coordinator: waiting clear_alarms settle");
        recovery_coordinator_poll();
        return;
    }

    reset_and_home();
}

/**
 * @brief  认领清故障等待终态后继续；已离开恢复则丢弃
 * @param  st  DONE 归位，FAILED 收尾失败
 */
static void finish_clear_wait(device_clear_alarms_status_t st)
{
    if (!claim_phase(RECOVERY_PHASE_WAIT_CLEAR, RECOVERY_PHASE_IDLE)) {
        return;
    }
    if (op_mode_get_current() != OP_MODE_RECOVERING) {
        LOG_WARN("recovery_coordinator: left recovering during clear wait");
        return;
    }
    if (st == DEVICE_CLEAR_ALARMS_FAILED) {
        LOG_WARN("recovery_coordinator: clear_alarms settle failed");
        complete_recovery(RECOVERY_RESULT_FAILED);
        return;
    }
    reset_and_home();
}

void recovery_coordinator_poll(void)
{
    const device_ops_t          *ops;
    device_clear_alarms_status_t st;

    if ((recovery_phase_t)atomic_load(&s_phase) != RECOVERY_PHASE_WAIT_CLEAR) {
        return;
    }
    if (op_mode_get_current() != OP_MODE_RECOVERING) {
        cancel_pending();
        return;
    }

    ops = device_ops_get();
    if ((ops == NULL) || (ops->poll_clear_alarms == NULL)) {
        finish_clear_wait(DEVICE_CLEAR_ALARMS_DONE);
        return;
    }

    st = ops->poll_clear_alarms();
    if (st == DEVICE_CLEAR_ALARMS_PENDING) {
        return;
    }
    finish_clear_wait(st);
}

sw_err_t recovery_coordinator_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_OP_MODE_RECOVERY_REQUESTED, on_recovery_requested},
        {EVT_OP_MODE_HOME_COMPLETED,     on_home_completed    },
        {EVT_OP_MODE_CHANGED,            on_mode_changed      },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("recovery_coordinator: init ok");
    return SW_OK;
}
