/**
 * @file    wash_orchestrator_stub.c
 * @brief   Demo/部分单测用：recovery / cutout / abort_home 空实现
 */

#include "application/orchestrators/abort_home_coordinator.h"
#include "application/orchestrators/safety_cutout_coordinator.h"
#include "application/recovery_service.h"
#include "common/sw_error.h"

sw_err_t recovery_service_init(void)
{
    return SW_OK;
}

sw_err_t safety_cutout_coordinator_init(void)
{
    return SW_OK;
}

sw_err_t abort_home_coordinator_init(void)
{
    return SW_OK;
}
