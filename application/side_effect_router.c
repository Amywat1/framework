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

sw_err_t side_effect_router_run(const dev_cmd_t *cmd, operational_mode_t mode_before)
{
    const machine_ops_t *ops;

    if (cmd == NULL) {
        return SW_ERR_PARAM;
    }

    ops = machine_ops_get();

    switch (cmd->body.kind) {
    case DEV_CMD_START_WASH:
        if ((ops == NULL) || (ops->start_wash == NULL)) {
            return SW_ERR_NOT_INIT;
        }
        return ops->start_wash(cmd->body.payload.start_wash.mode);

    case DEV_CMD_STOP_WASH:
        if ((ops == NULL) || (ops->abort_wash == NULL)) {
            return SW_ERR_NOT_INIT;
        }
        ops->abort_wash(WASH_ABORT_MANUAL);
        return SW_OK;

    case DEV_CMD_START_SELF_CHECK:
        run_self_check();
        return SW_OK;

    case DEV_CMD_MANUAL_ACTUATOR:
        if ((ops != NULL) && (ops->execute_manual_actuator != NULL)) {
            return ops->execute_manual_actuator(cmd->body.payload.manual_actuator.act_id,
                                                cmd->body.payload.manual_actuator.param);
        }
        return SW_ERR_NOT_INIT;

    case DEV_CMD_STOP_ALL_OUTPUTS:
        /* domain 已先切 STOPPED；仅当裁决前为 WASHING 时 abort，避免假 EVT_WASH_ABORTED */
        return run_stop_all_outputs(mode_before == OP_MODE_WASHING);

    case DEV_CMD_STOP_OPERATION:
    case DEV_CMD_RESUME_OPERATION:
    case DEV_CMD_RECOVER:
        /* 模式/旗标已在 domain 处理；RECOVER 异步编排由 RECOVERY_REQUESTED 触发 */
        return SW_OK;

    case DEV_CMD_NONE:
    case DEV_CMD_MAX:
        break;
    }

    return SW_ERR_PARAM;
}
