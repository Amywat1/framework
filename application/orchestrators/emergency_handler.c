/**
 * @file    emergency_handler.c
 * @brief   安全 LOCKOUT 统一动作处理
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#include "application/orchestrators/emergency_handler.h"
#include "application/orchestrators/wash_orchestrator.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"

static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    wash_orchestrator_abort();
    LOG_WARN("emergency_handler: LOCKOUT → wash aborted, outputs stopped");
}

sw_err_t emergency_handler_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_SAFETY_LOCKOUT, on_safety_lockout },
    };

    return event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
}
