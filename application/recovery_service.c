/**
 * @file    recovery_service.c
 * @brief   Recover 用例协调实现（清告警 + 异步全归位 + 验证）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "application/recovery_service.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "runtime/event_bus/event_bus.h"

#include <stdatomic.h>

static atomic_bool s_waiting_home = false;

/**
 * @brief  归位完成：仅处理 RECOVER 路径等待中的结果
 */
static void on_home_completed(const event_t *evt)
{
    recovery_result_t result;

    if (!atomic_exchange(&s_waiting_home, false)) {
        return;
    }

    result = (evt->param != 0U) ? RECOVERY_RESULT_IDLE : RECOVERY_RESULT_EXCEPTION;
    if (result != RECOVERY_RESULT_IDLE) {
        LOG_ERROR("recovery_service: home failed during recover");
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

    /* 1. 检查安全姿态：LOCKOUT 下无法恢复 */
    if (alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT) {
        LOG_WARN("recovery_service: still LOCKOUT before clear, abort recovery");
        goto done;
    }

    /* 2. 清除所有可恢复告警 */
    alarm_registry_recover_all();

    if (alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT) {
        LOG_WARN("recovery_service: still LOCKOUT after clear, abort recovery");
        goto done;
    }

    /* 3. 启动异步全归位；完成事件见 on_home_completed */
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
