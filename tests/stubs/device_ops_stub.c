/**
 * @file    device_ops_stub.c
 * @brief   单元测试用洗车 device_ops 桩实现
 */

#include "tests/stubs/device_ops_stub.h"

#include <stddef.h>

static int                s_start_count;
static int                s_abort_count;
static wash_mode_t        s_last_mode;
static wash_abort_cause_t s_last_abort_cause;

void device_ops_stub_reset(void)
{
    s_start_count      = 0;
    s_abort_count      = 0;
    s_last_mode        = TEST_WASH_MODE_A;
    s_last_abort_cause = WASH_ABORT_MANUAL;
}

int device_ops_stub_start_count(void)
{
    return s_start_count;
}

int device_ops_stub_abort_count(void)
{
    return s_abort_count;
}

wash_mode_t device_ops_stub_last_mode(void)
{
    return s_last_mode;
}

wash_abort_cause_t device_ops_stub_last_abort_cause(void)
{
    return s_last_abort_cause;
}

sw_err_t device_ops_stub_start(wash_mode_t mode)
{
    s_start_count++;
    s_last_mode = mode;
    return SW_OK;
}

void device_ops_stub_abort(wash_abort_cause_t cause)
{
    s_abort_count++;
    s_last_abort_cause = cause;
}

void device_ops_stub_bind(device_ops_t *ops)
{
    if (ops == NULL) {
        return;
    }
    ops->start_wash = device_ops_stub_start;
    ops->abort_wash = device_ops_stub_abort;
}
