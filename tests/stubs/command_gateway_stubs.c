/**
 * @file    command_gateway_stubs.c
 * @brief   command_gateway 单元测试用最小桩
 */

#include "framework/application/orchestrators/wash_orchestrator.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/safety/alarm_registry/alarm_registry.h"
#include "framework/domain/safety/model/safety_types.h"
#include "framework/common/sw_error.h"
#include "framework/domain/command_gateway/op_mode_types.h"

sw_err_t wash_orchestrator_init(void)
{
    return SW_OK;
}

sw_err_t wash_orchestrator_start(wash_mode_t mode)
{
    (void)mode;
    return SW_OK;
}

void wash_orchestrator_abort(wash_abort_cause_t cause)
{
    (void)cause;
}

bool wash_orchestrator_is_busy(void)
{
    return false;
}

sw_err_t gantry_home(void)
{
    return SW_OK;
}

sw_err_t alarm_registry_recover_all(void)
{
    return SW_OK;
}

safety_posture_t alarm_registry_safety_posture(void)
{
    return SAFETY_POSTURE_NOMINAL;
}

bool alarm_registry_has_blocking_active(void)
{
    return false;
}
