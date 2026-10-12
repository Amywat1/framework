/**
 * @file    device_ops_stub.c
 * @brief   单元测试用 device_ops 桩实现
 */

#include "tests/stubs/device_ops_stub.h"

#include <stddef.h>

static int                s_start_count;
static int                s_abort_count;
static int                s_clear_count;
static wash_mode_t        s_last_mode;
static uint64_t           s_last_job_id;
static wash_abort_cause_t s_last_abort_cause;
static bool               s_admit_ready = true;
static sw_err_t           s_start_result = SW_OK;

void device_ops_stub_reset(void)
{
    s_start_count      = 0;
    s_abort_count      = 0;
    s_clear_count      = 0;
    s_last_mode        = (wash_mode_t)0U;
    s_last_job_id      = 0U;
    s_last_abort_cause = WASH_ABORT_MANUAL;
    s_admit_ready      = true;
    s_start_result     = SW_OK;
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

uint64_t device_ops_stub_last_job_id(void)
{
    return s_last_job_id;
}

void device_ops_stub_set_start_result(sw_err_t result)
{
    s_start_result = result;
}

wash_abort_cause_t device_ops_stub_last_abort_cause(void)
{
    return s_last_abort_cause;
}

void device_ops_stub_set_admit_ready(bool ready)
{
    s_admit_ready = ready;
}

sw_err_t device_ops_stub_start(const job_desc_t *job)
{
    s_start_count++;
    if (job != NULL) {
        s_last_mode   = job->mode;
        s_last_job_id = job->job_id;
    }
    return s_start_result;
}

void device_ops_stub_abort(wash_abort_cause_t cause)
{
    s_abort_count++;
    s_last_abort_cause = cause;
}

void device_ops_stub_clear(void)
{
    s_clear_count++;
}

static bool stub_admit_ready(void)
{
    return s_admit_ready;
}

void device_ops_stub_bind(device_ops_t *ops)
{
    if (ops == NULL) {
        return;
    }
    ops->start_job      = device_ops_stub_start;
    ops->abort_jobs     = device_ops_stub_abort;
    ops->clear_machine  = device_ops_stub_clear;
    ops->is_admit_ready = stub_admit_ready;
}
