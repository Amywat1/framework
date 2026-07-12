/**
 * @file    self_check_service.c
 * @brief   完整自检流程协调实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "application/self_check_service.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/command_gateway/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/safety_types.h"
#include "runtime/event_bus/event_bus.h"

static void publish_completed(bool land_exception)
{
    (void)event_publish(EVT_OP_MODE_SELF_CHECK_COMPLETED, land_exception ? 1U : 0U);
}

sw_err_t self_check_service_init(void)
{
    LOG_INFO("self_check_service: init ok");
    return SW_OK;
}

sw_err_t self_check_service_start(void)
{
    bool land_exception = false;

    if (op_mode_is_estop_active()) {
        land_exception = true;
    } else if (alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT) {
        land_exception = true;
    } else if (alarm_registry_has_blocking_active()) {
        land_exception = true;
    }

    publish_completed(land_exception);
    LOG_INFO("self_check_service: completed land_exception=%d", (int)land_exception);
    return SW_OK;
}
