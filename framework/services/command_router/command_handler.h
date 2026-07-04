/**
 * @file    command_handler.h
 * @brief   命令处理器接口（实现 command_port，将 cmd_t 路由至 event_bus）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    command_port 的 inbound port 实现，属于基础设施层。
 *          调用方（wiring）注册后，各 adapter 通过 command_port 注入命令，
 *          处理器负责校验并将命令发布为 EVT_CMD_* 事件。
 */

#ifndef INFRASTRUCTURE_SERVICES_COMMAND_HANDLER_H
#define INFRASTRUCTURE_SERVICES_COMMAND_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册命令处理器实现到 command_port
 */
void command_handler_register(void);

#ifdef __cplusplus
}
#endif

#endif /* INFRASTRUCTURE_SERVICES_COMMAND_HANDLER_H */
