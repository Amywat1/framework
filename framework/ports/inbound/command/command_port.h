/**
 * @file    command_port.h
 * @brief   外部命令接入端口接口（MQTT 下行 / CLI / BLE 等统一注入点）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    各命令来源解析命令后通过 command_port.inject() 注入；
 *          command_gateway 在 event_dispatch 线程仲裁并执行副作用。
 */

#ifndef PORTS_CLOUD_COMMAND_PORT_H
#define PORTS_CLOUD_COMMAND_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/wash/model/wash_types.h"
#include "framework/common/sw_error.h"

/**
 * @brief  命令类型（port 层 DTO）
 */
typedef enum
{
    CMD_NONE            = 0,
    CMD_START_WASH,
    CMD_STOP_WASH,
    CMD_STOP_OPERATION,
    CMD_RESUME_OPERATION,
    CMD_RESET_FAULT,
    CMD_HOME_DEVICE,
    CMD_ENTER_MANUAL,
    CMD_MANUAL_ACTUATOR,
    CMD_START_SELF_CHECK,
    CMD_RECOVER,
    CMD_MAX
} cmd_type_t;

typedef struct
{
    cmd_type_t type;
    union
    {
        struct
        {
            wash_mode_t mode;
        } start_wash;
    } payload;
} cmd_t;

typedef struct
{
    /**
     * @brief  注入一条命令
     * @param  cmd  已解析的命令
     * @retval SW_OK / SW_ERR_STATE / SW_ERR_BUSY / SW_ERR_TIMEOUT
     */
    sw_err_t (*inject)(const cmd_t *cmd);
} command_port_ops_t;

void                      command_port_register(const command_port_ops_t *ops);
const command_port_ops_t *command_port_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_CLOUD_COMMAND_PORT_H */
