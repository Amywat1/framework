/**
 * @file    command_bridge.h
 * @brief   命令端口桥接适配器接口（command_port → event_bus）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    实现 command_port_ops_t.inject()，将 cmd_t 转换为 EVT_CMD_* 事件。
 *          命令来源（MQTT、BLE、CLI 等）只需持有 command_port 接口，
 *          无需直接依赖 event_bus。
 */

#ifndef ADAPTERS_UI_COMMAND_BRIDGE_H
#define ADAPTERS_UI_COMMAND_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册命令桥接实现到 command_port
 */
void command_bridge_register(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_UI_COMMAND_BRIDGE_H */
