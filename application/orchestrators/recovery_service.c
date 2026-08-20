/**
 * @file    recovery_service.c
 * @brief   Recover 用例协调实现（复位锁存告警 + 异步全归位 + 阻塞告警验证）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "application/orchestrators/recovery_service.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/machine/machine_ops_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"

#include <stdatomic.h>

static atomic_bool s_waiting_home = false;

/**
 * @brief  取消进行中的归位等待（离开 RECOVERING 时调用）
 */
static void cancel_pending(void)
{
    if (atomic_exchange(&s_waiting_home, false)) {
        LOG_WARN("recovery_service: pending home wait cancelled");
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
 * @brief  归位完成：仅处理仍处于 RECOVERING 且等待中的结果
 */
static void on_home_completed(const event_t *evt)
{
    recovery_result_t result;
    bool              home_success;
    bool              blocking_active;

    /* 模式可能已先被 STOP_ALL/急停切走；事件异步，须再守一层 */
    if (op_mode_get_current() != OP_MODE_RECOVERING) {
        cancel_pending();
        return;
    }

    if (!atomic_exchange(&s_waiting_home, false)) {
        return;
    }

    home_success = evt->param != 0U;
    alarm_registry_reset_all();
    blocking_active = alarm_registry_has_blocking_active();
    result          = (home_success && !blocking_active) ? RECOVERY_RESULT_IDLE : RECOVERY_RESULT_FAILED;
    if (!home_success) {
        LOG_ERROR("recovery_service: home failed during recover");
    } else if (blocking_active) {
        LOG_WARN("recovery_service: blocking alarm remains after home");
    }

    (void)event_publish_required(EVT_OP_MODE_RECOVERY_COMPLETED, (uint32_t)result);
    LOG_INFO("recovery_service: completed result=%d", (int)result);
}

static void on_recovery_requested(const event_t *evt)
{
    recovery_result_t    result = RECOVERY_RESULT_FAILED;
    const machine_ops_t *ops;

    (void)evt;
    atomic_store(&s_waiting_home, false);

    /* 先复位故障条件已经消失的锁存告警，再检查是否仍处于 LOCKOUT。 */
    alarm_registry_reset_all();
    if (alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT) {
        LOG_WARN("recovery_service: still LOCKOUT after reset, abort recovery");
        goto done;
    }

    /* 启动异步全归位；完成时统一验证阻塞告警。 */
    ops = machine_ops_get();
    if ((ops == NULL) || (ops->home_device == NULL)) {
        LOG_ERROR("recovery_service: home_device not available");
        goto done;
    }

    atomic_store(&s_waiting_home, true);
    if (ops->home_device() != SW_OK) {
        atomic_store(&s_waiting_home, false);
        LOG_ERROR("recovery_service: home_device start failed");
        goto done;
    }

    LOG_INFO("recovery_service: home started, waiting HOME_COMPLETED");
    return;

done:
    (void)event_publish_required(EVT_OP_MODE_RECOVERY_COMPLETED, (uint32_t)result);
    LOG_INFO("recovery_service: completed result=%d", (int)result);
}

sw_err_t recovery_service_init(void)
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

    LOG_INFO("recovery_service: init ok");
    return SW_OK;
}
