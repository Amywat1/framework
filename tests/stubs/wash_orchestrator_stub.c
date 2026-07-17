/**
 * @file    wash_orchestrator_stub.c
 * @brief   Demo/部分单测用：recovery / emergency 空实现（洗车已走 machine_ops）
 */

#include "application/orchestrators/emergency_handler.h"
#include "application/recovery_service.h"
#include "common/sw_error.h"

sw_err_t recovery_service_init(void)
{
    return SW_OK;
}

sw_err_t emergency_handler_init(void)
{
    return SW_OK;
}
