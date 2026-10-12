/**
 * @file    side_effect_router.c
 * @brief   命令副作用路由实现（含自检完成判定）
 */

#include "application/side_effect_router.h"

#include "application/job_admit.h"
#include "common/event_types.h"
#include "common/log.h"
#include "domain/job/job_types.h"
#include "domain/job/machine_profile.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"

#include <stddef.h>

/**
 * @brief  执行自检判定并发布完成事件
 */
static sw_err_t run_self_check(void)
{
    bool land_fault = false;

    if (op_mode_is_estop_active()) {
        land_fault = true;
    } else if (alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT) {
        land_fault = true;
    } else if (alarm_registry_has_blocking_active()) {
        land_fault = true;
    }

    sw_err_t ret = event_publish_required(EVT_OP_MODE_SELF_CHECK_COMPLETED, land_fault ? 1U : 0U);

    LOG_INFO("side_effect_router: self_check completed land_fault=%d", (int)land_fault);
    return ret;
}

static sw_err_t run_stop_all_outputs(bool abort_active)
{
    const device_ops_t *ops = device_ops_get();
    sw_err_t            ret;
    unsigned            dropped;

    if ((ops == NULL) || (ops->stop_all_outputs == NULL)) {
        return SW_ERR_NOT_INIT;
    }

    /* 先撤占用：与进行中的放行互斥，裁决后才启动的作业也会被计入并中止。 */
    dropped = job_admit_drop_jobs(true);
    ret     = ops->stop_all_outputs();
    if ((abort_active || (dropped > 0U)) && (ops->abort_jobs != NULL)) {
        ops->abort_jobs(WASH_ABORT_STOP_ALL);
    }
    return ret;
}

sw_err_t side_effect_router_run(const dev_cmd_t *cmd, operational_mode_t mode_before)
{
    const device_ops_t *ops;
    job_desc_t          order;
    machine_profile_t   profile;

    if (cmd == NULL) {
        return SW_ERR_PARAM;
    }

    ops = device_ops_get();

    switch (cmd->body.kind) {
    case DEV_CMD_SUBMIT_ORDER:
        order.order_id       = cmd->body.payload.submit_order.order_id;
        order.job_id         = 0U;
        order.correlation_id = cmd->meta.correlation_id;
        order.mode           = cmd->body.payload.submit_order.mode;
        order.enqueued_ms    = 0U;
        return job_admit_submit(&order);

    case DEV_CMD_CANCEL_ORDER:
        return job_admit_cancel(cmd->body.payload.cancel_order.order_id);

    case DEV_CMD_CLEAR_MACHINE:
        if ((ops == NULL) || (ops->abort_jobs == NULL)) {
            return SW_ERR_NOT_INIT;
        }
        /* 先撤占用再中止：随后到达的 EVT_JOB_ABORTED 被放行核忽略，不会二次请求清障。 */
        (void)job_admit_drop_jobs(false);
        ops->abort_jobs(WASH_ABORT_MANUAL);
        return SW_OK;

    case DEV_CMD_START_SELF_CHECK:
        return run_self_check();

    case DEV_CMD_MANUAL_ACTUATOR:
        if ((ops != NULL) && (ops->execute_manual_actuator != NULL)) {
            return ops->execute_manual_actuator(cmd->body.payload.manual_actuator.act_id,
                                                cmd->body.payload.manual_actuator.param);
        }
        return SW_ERR_NOT_INIT;

    case DEV_CMD_STOP_ALL_OUTPUTS:
        return run_stop_all_outputs(mode_before == OP_MODE_ACTIVE);

    case DEV_CMD_SET_SERVICE:
        if (!cmd->body.payload.service.enabled) {
            profile = machine_profile_get();
            if (profile.clear_on_service_off) {
                job_admit_clear_orders();
            }
        }
        return SW_OK;

    case DEV_CMD_RECOVER:
        return SW_OK;

    case DEV_CMD_NONE:
    case DEV_CMD_MAX:
        break;
    }

    return SW_ERR_PARAM;
}
