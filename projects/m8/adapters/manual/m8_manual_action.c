/**
 * @file    m8_manual_action.c
 * @brief   M8 手动机构动作统一入口实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    运动类动作前先经 command_port 注入 CMD_MANUAL_ACTUATOR，
 *          由 command_gateway/OperationalMode 命令矩阵统一裁决。
 */

#include "projects/m8/adapters/manual/m8_manual_action.h"
#include "projects/m8/domain/mechanism/gantry.h"
#include "projects/m8/domain/mechanism/m8_brush_rotation.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/common/log.h"
#include <stddef.h>

/**
 * @brief  经命令网关校验手动点动权限
 * @retval SW_OK         允许
 * @retval SW_ERR_STATE  当前模式不允许或急停激活
 */
static sw_err_t request_manual_actuator(void)
{
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t                       cmd;

    if ((cp == NULL) || (cp->inject == NULL))
    {
        LOG_WARN("m8_manual_action: command_port not registered");
        return SW_ERR_NOT_INIT;
    }

    cmd.type = CMD_MANUAL_ACTUATOR;
    return cp->inject(&cmd);
}

sw_err_t m8_manual_gantry_fwd(int speed_gear)
{
    sw_err_t ret = request_manual_actuator();

    if (ret != SW_OK)
    {
        return ret;
    }

    return gantry_move_fwd(speed_gear, NULL);
}

sw_err_t m8_manual_gantry_rev(int speed_gear)
{
    sw_err_t ret = request_manual_actuator();

    if (ret != SW_OK)
    {
        return ret;
    }

    return gantry_move_rev(speed_gear, NULL);
}

sw_err_t m8_manual_gantry_stop(void)
{
    return gantry_stop();
}

sw_err_t m8_manual_brush_start(brush_id_t id, int speed_gear)
{
    sw_err_t ret = request_manual_actuator();

    if (ret != SW_OK)
    {
        return ret;
    }

    return brush_start(id, speed_gear);
}

sw_err_t m8_manual_brush_stop(brush_id_t id)
{
    return brush_stop(id);
}
