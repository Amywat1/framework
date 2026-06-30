/**
 * @file    event_bridge.h
 * @brief   命令端口桥接适配器接口（command_port → event_bus）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    实现 command_port_ops_t.inject()：command_guard 校验 → 发布 EVT_CMD_*。
 *          CLI / MQTT / 仿真控制台等统一经 command_port 注入，不直接依赖 event_bus。
 */

#ifndef ADAPTERS_COMMAND_EVENT_BRIDGE_H
#define ADAPTERS_COMMAND_EVENT_BRIDGE_H

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

#endif /* ADAPTERS_COMMAND_EVENT_BRIDGE_H */
