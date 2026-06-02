/**
 * @file    command_port.h
 * @brief   外部命令接入端口接口（MQTT 下行 / CLI / BLE 等统一注入点）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    各命令来源（aliyun_command_adapter、cli、sim_console 等）解析命令后，
 *          通过 command_port.inject() 注入；bridge 经 command_guard 校验后发布 EVT_CMD_*，
 *          由 device_fsm 在事件线程消费。
 */

#ifndef PORTS_CLOUD_COMMAND_PORT_H
#define PORTS_CLOUD_COMMAND_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/command.h"
#include "common/sw_error.h"

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
