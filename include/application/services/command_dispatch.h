#ifndef APPLICATION_SERVICES_COMMAND_DISPATCH_H
#define APPLICATION_SERVICES_COMMAND_DISPATCH_H

#include "application/ports/inbound/command_port.h"
#include "application/ports/outbound/scheduler_sync_port.h"

/**
 * @file command_dispatch.h
 * @brief 声明命令分发组件接口。
 *
 * @details 封装完整命令处理协议：解析 → 状态预检 → 触发提交
 *          → 同步 tick 执行 → 响应重建。实现 command_port_t 入站端口，
 *          通过 scheduler_sync_port_t 出站端口驱动调度器同步执行。
 */

/**
 * @brief 命令分发组件实例。
 */
typedef struct command_dispatch_t
{
    scheduler_sync_port_t scheduler_sync_port; /**< 调度器同步执行出站端口绑定。 */
} command_dispatch_t;

/**
 * @brief 初始化命令分发组件。
 * @param dispatch 组件实例，不能为空。
 * @param scheduler_sync_port 调度器同步执行端口绑定，不能为空。
 */
void command_dispatch_init(command_dispatch_t *dispatch, const scheduler_sync_port_t *scheduler_sync_port);

/**
 * @brief 将组件实例转为命令入站端口。
 * @param dispatch 组件实例，不能为空。
 * @return 命令入站端口；dispatch 为空时返回函数指针全为 `0` 的空端口。
 */
command_port_t command_dispatch_as_port(command_dispatch_t *dispatch);

#endif
