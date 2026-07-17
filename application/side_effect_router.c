/**
 * @file    side_effect_router.c
 * @brief   命令副作用路由实现
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "application/side_effect_router.h"

#include "application/orchestrators/wash_orchestrator.h"
#include "application/self_check_service.h"
#include "common/event_types.h"
#include "common/log.h"
#include "domain/op_mode/op_mode_types.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "runtime/event_bus/event_bus.h"

#include <stddef.h>

sw_err_t side_effect_router_run(dev_cmd_effect_t effect, const dev_cmd_t *cmd)
{
    const machine_ops_t *ops;
    sw_err_t             ret;

    if (cmd == NULL) {
        return SW_ERR_PARAM;
    }

    switch (effect) {
    case DEV_CMD_EFFECT_NONE:
        return SW_OK;

    case DEV_CMD_EFFECT_START_WASH:
        return wash_orchestrator_start(cmd->body.payload.start_wash.mode);

    case DEV_CMD_EFFECT_STOP_WASH:
        wash_orchestrator_abort(WASH_ABORT_MANUAL);
        return SW_OK;

    case DEV_CMD_EFFECT_SELF_CHECK:
        self_check_service_start();
        return SW_OK;

    case DEV_CMD_EFFECT_HOME_DEVICE:
        ops = machine_ops_get();
        if ((ops != NULL) && (ops->home_device != NULL)) {
            ret = ops->home_device();
            /* 通知 op_mode_bridge 归位结果，触发 HOMING → IDLE/EXCEPTION */
            (void)event_publish(EVT_OP_MODE_HOME_COMPLETED, ret == SW_OK ? 1U : 0U);
            if (ret != SW_OK) {
                LOG_ERROR("side_effect_router: home_device failed ret=%d", (int)ret);
            }
            return ret;
        }
        (void)event_publish(EVT_OP_MODE_HOME_COMPLETED, 0U);
        return SW_ERR_NOT_INIT;

    case DEV_CMD_EFFECT_MANUAL_ACTUATOR:
        ops = machine_ops_get();
        if ((ops != NULL) && (ops->execute_manual_actuator != NULL)) {
            return ops->execute_manual_actuator(cmd->body.payload.manual_actuator.act_id,
                                                cmd->body.payload.manual_actuator.param);
        }
        return SW_ERR_NOT_INIT;

    case DEV_CMD_EFFECT_STOP_ALL_OUTPUTS:
        ops = machine_ops_get();
        if ((ops != NULL) && (ops->stop_all_outputs != NULL)) {
            return ops->stop_all_outputs();
        }
        return SW_ERR_NOT_INIT;

    default:
        return SW_ERR_PARAM;
    }
}
