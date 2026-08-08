/**
 * @file    wash_orchestrator_stub.c
 * @brief   部分单测用：recovery / safety_session 空实现
 */

#include "application/orchestrators/safety_session_coordinator.h"
#include "application/recovery_service.h"
#include "common/sw_error.h"

sw_err_t recovery_service_init(void)
{
    return SW_OK;
}

sw_err_t safety_session_coordinator_init(void)
{
    return SW_OK;
}
