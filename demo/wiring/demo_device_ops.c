/**
 * @file    demo_device_ops.c
 * @brief   Demo 仿真 device_ops：下单后由放行核调用 start_job
 */

#include "common/event_types.h"
#include "common/sw_error.h"
#include "domain/job/job_types.h"
#include "domain/job/machine_profile.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "runtime/event_bus/event_bus.h"

static bool s_admit_ready = true;

static void demo_clear_machine(void)
{
    (void)event_publish(EVT_CLEAR_MACHINE_DONE, (uint32_t)SW_OK);
}

static sw_err_t demo_start_job(const job_desc_t *job)
{
    wash_mode_t mode = (job != NULL) ? job->mode : (wash_mode_t)0U;
    uint32_t    id   = (job != NULL) ? (uint32_t)job->job_id : 0U;

    (void)event_publish(EVT_JOB_STARTED, job_started_evt_param(mode));
    (void)event_publish(EVT_JOB_COMPLETED, id);
    return SW_OK;
}

static void demo_abort_jobs(wash_abort_cause_t cause)
{
    (void)cause;
    (void)event_publish(EVT_JOB_ABORTED, 0U);
}

static sw_err_t demo_home_device(void)
{
    (void)event_publish(EVT_OP_MODE_HOME_COMPLETED, 1U);
    return SW_OK;
}

static sw_err_t demo_manual_actuator(uint32_t act_id, int32_t param)
{
    (void)act_id;
    (void)param;
    return SW_OK;
}

static sw_err_t demo_stop_all_outputs(void)
{
    return SW_OK;
}

static bool demo_is_admit_ready(void)
{
    return s_admit_ready;
}

static const device_ops_t s_demo_device_ops = {
    .clear_machine           = demo_clear_machine,
    .start_job               = demo_start_job,
    .abort_jobs              = demo_abort_jobs,
    .home_device             = demo_home_device,
    .execute_manual_actuator = demo_manual_actuator,
    .stop_all_outputs        = demo_stop_all_outputs,
    .is_admit_ready          = demo_is_admit_ready,
};

/* 单车、等场区清空、中止即清障、停机清队、订单不过期 */
static const machine_profile_t s_demo_profile = {
    .max_active_jobs         = 1U,
    .max_queued_orders       = JOB_ORDER_QUEUE_CAP,
    .hold_after_last_job     = true,
    .clear_on_job_abort      = true,
    .clear_orders_on_stopped = true,
    .max_order_age_ms        = 0U,
};

/**
 * @brief  注册 Demo device_ops 与机型 profile
 */
sw_err_t demo_device_ops_register(void)
{
    device_ops_register(&s_demo_device_ops);
    return machine_profile_register(&s_demo_profile);
}
