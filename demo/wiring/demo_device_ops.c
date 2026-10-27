/**
 * @file    demo_device_ops.c
 * @brief   Demo 整机端口：device_ops、机型 profile、入场占用
 *
 * @note    start_job / home_device / clear_machine 只启动并回报事件，禁止在
 *          回调里跑完整工艺。Demo 无方案引擎：start_job 只发 STARTED，由后续
 *          STOP_ALL 走 abort；home / clear 同步发完成事件，产品须改成会话完成
 *          后再发。可选口：次数授权已接 wash_quota；无可复位硬件时清故障留空。
 */

#include "application/wash_quota.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "domain/job/job_types.h"
#include "domain/job/machine_profile.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/ports/outbound/device/entry_occupancy_port.h"
#include "runtime/event_bus/event_bus.h"

static bool s_admit_ready = true;

static void demo_clear_machine(void)
{
    (void)event_publish(EVT_CLEAR_MACHINE_DONE, (uint32_t)SW_OK);
}

static sw_err_t demo_start_job(const job_desc_t *job)
{
    wash_mode_t mode = (job != NULL) ? job->mode : (wash_mode_t)0U;

    (void)event_publish(EVT_JOB_STARTED, job_started_evt_param(mode));
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

static entry_occupancy_t demo_entry_occupancy(void)
{
    entry_occupancy_t view;

    view.available = true;
    view.phase     = ENTRY_OCC_READY;
    return view;
}

static const device_ops_t s_demo_device_ops = {
    .clear_machine           = demo_clear_machine,
    .start_job               = demo_start_job,
    .abort_jobs              = demo_abort_jobs,
    .home_device             = demo_home_device,
    .execute_manual_actuator = demo_manual_actuator,
    .stop_all_outputs        = demo_stop_all_outputs,
    .is_admit_ready          = demo_is_admit_ready,
    .admit_check             = wash_quota_check_start,
    .admit_consume           = wash_quota_consume_one,
    .clear_alarms            = NULL,
    .poll_clear_alarms       = NULL,
};

/*
 * 单车：同时一单、队深 1、洗完 HOLD、中止清障、停机清队。
 * 连续多车把 max_active_jobs / max_queued_orders 调高；有入队有效期则填毫秒。
 */
static const machine_profile_t s_demo_profile = {
    .max_active_jobs         = 1U,
    .max_queued_orders       = 1U,
    .hold_after_last_job     = true,
    .clear_on_job_abort      = true,
    .clear_orders_on_stopped = true,
    .max_order_age_ms        = 0U,
};

static const entry_occupancy_ops_t s_demo_occupancy = {
    .get = demo_entry_occupancy,
};

/**
 * @brief  注册 Demo device_ops、机型 profile 与入场占用
 *
 * @retval SW_OK     已注册
 * @retval 其它      profile 或占用口拒绝
 *
 * @note    占用口报物理相位，不把运营态写进去。无入场灯/延迟开洗/视觉的项目
 *          可不注册，也不要声明 PORT_REQ_ENTRY_OCCUPANCY。
 */
sw_err_t demo_device_ops_register(void)
{
    sw_err_t ret = machine_profile_register(&s_demo_profile);

    if (ret != SW_OK) {
        return ret;
    }
    ret = device_ops_register(&s_demo_device_ops);
    if (ret != SW_OK) {
        return ret;
    }
    return entry_occupancy_register(&s_demo_occupancy);
}
