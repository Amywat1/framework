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
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "domain/ports/outbound/machine/machine_ops_port.h"
#include "runtime/event_bus/event_bus.h"

#include <stdatomic.h>

static atomic_bool s_waiting_home = false;

/**
 * @brief  归位完成：仅处理 RECOVER 路径等待中的结果
 */
static void on_home_completed(const event_t *evt)
{
    recovery_result_t result;
    bool              home_success;
    bool              blocking_active;

    if (!atomic_exchange(&s_waiting_home, false)) {
        return;
    }

    home_success = evt->param != 0U;
    alarm_registry_reset_all();
    blocking_active = alarm_registry_has_blocking_active();
    result          = (home_success && !blocking_active) ? RECOVERY_RESULT_IDLE : RECOVERY_RESULT_EXCEPTION;
    if (!home_success) {
        LOG_ERROR("recovery_service: home failed during recover");
    } else if (blocking_active) {
        LOG_WARN("recovery_service: blocking alarm remains after home");
    }

    (void)event_publish(EVT_OP_MODE_RECOVERY_COMPLETED, (uint32_t)result);
    LOG_INFO("recovery_service: completed result=%d", (int)result);
}

static void on_recovery_requested(const event_t *evt)
{
    recovery_result_t    result = RECOVERY_RESULT_EXCEPTION;
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
    (void)event_publish(EVT_OP_MODE_RECOVERY_COMPLETED, (uint32_t)result);
    LOG_INFO("recovery_service: completed result=%d", (int)result);
}

sw_err_t recovery_service_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_OP_MODE_RECOVERY_REQUESTED, on_recovery_requested},
        {EVT_OP_MODE_HOME_COMPLETED,     on_home_completed    },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("recovery_service: init ok");
    return SW_OK;
}
