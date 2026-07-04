/**
 * @file    command_port.h
 * @brief   外部命令接入端口接口（MQTT 下行 / CLI / BLE 等统一注入点）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    各命令来源（snack_cloud_adapter、cli、sim_console 等）解析命令后，
 *          通过 command_port.inject() 注入；bridge 经 command_guard 校验后发布 EVT_CMD_*，
 *          由 device_fsm 在事件线程消费。
 */

#ifndef PORTS_CLOUD_COMMAND_PORT_H
#define PORTS_CLOUD_COMMAND_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/wash/model/wash_types.h"
#include "framework/common/sw_error.h"

/* -------------------------------------------------------------------------
 * 命令类型（port 层 DTO：外部命令 → 内部处理的边界传输对象）
 * ------------------------------------------------------------------------- */
typedef enum
{
    CMD_NONE            = 0,
    CMD_START_WASH,         /* 启动洗车，payload: wash_mode */
    CMD_STOP_WASH,          /* 中止当前洗车 */
    CMD_STOP_OPERATION,     /* 关闭运营（不再接单）*/
    CMD_RESUME_OPERATION,   /* 恢复运营 */
    CMD_RESET_FAULT,        /* 故障复位（清除 MANUAL 类报警）*/
    CMD_HOME_DEVICE,        /* 手动归位 */
    CMD_MAX
} cmd_type_t;

typedef struct
{
    cmd_type_t type;
    union
    {
        struct
        {
            wash_mode_t mode; /* CMD_START_WASH 时使用 */
        } start_wash;
    } payload;
} cmd_t;

/* -------------------------------------------------------------------------
 * 命令接入操作表
 * ------------------------------------------------------------------------- */
typedef struct
{
    /**
     * @brief  注入一条命令（adapter 层解析完成后调用）
     * @param  cmd  已解析的命令
     * @retval SW_OK / SW_ERR_STATE / SW_ERR_BUSY
     */
    sw_err_t (*inject)(const cmd_t *cmd);
} command_port_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取
 * ------------------------------------------------------------------------- */
void                      command_port_register(const command_port_ops_t *ops);
const command_port_ops_t *command_port_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_CLOUD_COMMAND_PORT_H */
