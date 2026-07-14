/**
 * @file    recovery_service.c
 * @brief   Recover 用例协调实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "application/recovery_service.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/command_gateway/op_mode_types.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"

static void on_recovery_requested(const event_t *evt)
{
    recovery_result_t result = RECOVERY_RESULT_IDLE;

    (void)evt;

    if (alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT) {
        result = RECOVERY_RESULT_EXCEPTION;
    } else {
        (void)alarm_registry_recover_all();
        if (alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT) {
            result = RECOVERY_RESULT_EXCEPTION;
        }
    }

    (void)event_publish(EVT_OP_MODE_RECOVERY_COMPLETED, (uint32_t)result);
    LOG_INFO("recovery_service: completed result=%d", (int)result);
}

sw_err_t recovery_service_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_OP_MODE_RECOVERY_REQUESTED, on_recovery_requested},
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("recovery_service: init ok");
    return SW_OK;
}
