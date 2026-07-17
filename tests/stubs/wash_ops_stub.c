/**
 * @file    wash_ops_stub.c
 * @brief   单元测试用洗车 machine_ops 桩实现
 */

#include "tests/stubs/wash_ops_stub.h"

#include <stddef.h>

static int                s_start_count;
static int                s_abort_count;
static wash_mode_t        s_last_mode;
static wash_abort_cause_t s_last_abort_cause;

void wash_ops_stub_reset(void)
{
    s_start_count      = 0;
    s_abort_count      = 0;
    s_last_mode        = WASH_MODE_STANDARD;
    s_last_abort_cause = WASH_ABORT_MANUAL;
}

int wash_ops_stub_start_count(void)
{
    return s_start_count;
}

int wash_ops_stub_abort_count(void)
{
    return s_abort_count;
}

wash_mode_t wash_ops_stub_last_mode(void)
{
    return s_last_mode;
}

wash_abort_cause_t wash_ops_stub_last_abort_cause(void)
{
    return s_last_abort_cause;
}

sw_err_t wash_ops_stub_start(wash_mode_t mode)
{
    s_start_count++;
    s_last_mode = mode;
    return SW_OK;
}

void wash_ops_stub_abort(wash_abort_cause_t cause)
{
    s_abort_count++;
    s_last_abort_cause = cause;
}

void wash_ops_stub_bind(machine_ops_t *ops)
{
    if (ops == NULL) {
        return;
    }
    ops->start_wash = wash_ops_stub_start;
    ops->abort_wash = wash_ops_stub_abort;
}
