/**
 * @file    wash_orchestrator_stub.c
 * @brief   洗车编排器链接桩（供 command_gateway / side_effect_router 单元测试使用）
 */

#include "tests/stubs/wash_orchestrator_stub.h"

#include "application/orchestrators/wash_orchestrator.h"

static int                s_start_count;
static int                s_abort_count;
static wash_mode_t        s_last_mode;
static wash_abort_cause_t s_last_abort_cause;

void wash_orchestrator_stub_reset(void)
{
    s_start_count      = 0;
    s_abort_count      = 0;
    s_last_mode        = WASH_MODE_STANDARD;
    s_last_abort_cause = WASH_ABORT_MANUAL;
}

int wash_orchestrator_stub_start_count(void)
{
    return s_start_count;
}

int wash_orchestrator_stub_abort_count(void)
{
    return s_abort_count;
}

wash_mode_t wash_orchestrator_stub_last_mode(void)
{
    return s_last_mode;
}

wash_abort_cause_t wash_orchestrator_stub_last_abort_cause(void)
{
    return s_last_abort_cause;
}

sw_err_t wash_orchestrator_init(void)
{
    return SW_OK;
}

sw_err_t wash_orchestrator_start(wash_mode_t mode)
{
    s_start_count++;
    s_last_mode = mode;
    return SW_OK;
}

void wash_orchestrator_abort(wash_abort_cause_t cause)
{
    s_abort_count++;
    s_last_abort_cause = cause;
}

bool wash_orchestrator_is_busy(void)
{
    return false;
}
