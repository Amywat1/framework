/**
 * @file    command_gateway_stubs.c
 * @brief   command_gateway 单元测试用最小桩
 */

#include "framework/application/orchestrators/wash_orchestrator.h"
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
