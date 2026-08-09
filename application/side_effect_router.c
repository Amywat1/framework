/**
 * @file    side_effect_router.c
 * @brief   命令副作用路由实现（含自检完成判定）
 */

#include "application/side_effect_router.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/machine/machine_ops_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"

#include <stddef.h>

/**
 * @brief  执行自检判定并发布完成事件
 * @note   按急停 / LOCKOUT / 阻塞报警决定 land_fault；模式一律回到 STOPPED。
 */
static void run_self_check(void)
{
    bool land_fault = false;

    if (op_mode_is_estop_active()) {
        land_fault = true;
    } else if (alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT) {
        land_fault = true;
    } else if (alarm_registry_has_blocking_active()) {
        land_fault = true;
    }

    (void)event_publish(EVT_OP_MODE_SELF_CHECK_COMPLETED, land_fault ? 1U : 0U);
    LOG_INFO("side_effect_router: self_check completed land_fault=%d", (int)land_fault);
}

/**
 * @brief  切断全部输出；可选中止洗车会话
 */
static sw_err_t run_stop_all_outputs(bool abort_wash_session)
{
    const machine_ops_t *ops = machine_ops_get();
    sw_err_t             ret;

    if ((ops == NULL) || (ops->stop_all_outputs == NULL)) {
        return SW_ERR_NOT_INIT;
    }

    ret = ops->stop_all_outputs();
    if (abort_wash_session && (ops->abort_wash != NULL)) {
        ops->abort_wash(WASH_ABORT_STOP_ALL);
    }
    return ret;
}

sw_err_t side_effect_router_run(dev_cmd_effect_t effect, const dev_cmd_t *cmd)
{
    const machine_ops_t *ops;

    if (cmd == NULL) {
        return SW_ERR_PARAM;
    }

    ops = machine_ops_get();

    switch (effect) {
    case DEV_CMD_EFFECT_NONE:
        return SW_OK;

    case DEV_CMD_EFFECT_START_WASH:
        if ((ops == NULL) || (ops->start_wash == NULL)) {
            return SW_ERR_NOT_INIT;
        }
        return ops->start_wash(cmd->body.payload.start_wash.mode);

    case DEV_CMD_EFFECT_STOP_WASH:
        if ((ops == NULL) || (ops->abort_wash == NULL)) {
            return SW_ERR_NOT_INIT;
        }
        ops->abort_wash(WASH_ABORT_MANUAL);
        return SW_OK;

    case DEV_CMD_EFFECT_SELF_CHECK:
        run_self_check();
        return SW_OK;

    case DEV_CMD_EFFECT_MANUAL_ACTUATOR:
        if ((ops != NULL) && (ops->execute_manual_actuator != NULL)) {
            return ops->execute_manual_actuator(cmd->body.payload.manual_actuator.act_id,
                                                cmd->body.payload.manual_actuator.param);
        }
        return SW_ERR_NOT_INIT;

    case DEV_CMD_EFFECT_STOP_ALL_OUTPUTS:
        return run_stop_all_outputs(false);

    case DEV_CMD_EFFECT_STOP_ALL_OUTPUTS_AND_ABORT:
        return run_stop_all_outputs(true);

    default:
        return SW_ERR_PARAM;
    }
}
