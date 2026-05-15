#ifndef ADAPTERS_INBOUND_CLI_COMMAND_ADAPTER_H
#define ADAPTERS_INBOUND_CLI_COMMAND_ADAPTER_H

#include <stddef.h>

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

/**
 * @file cli_command_adapter.h
 * @brief 定义主控命令行输入适配器。
 *
 * @note 本适配器只负责 CLI 协议适配与响应拼装，不拥有调度状态或业务状态写入口。
 * @note 统一正式入口编排由 `process_formal_command` 负责，本适配器不得演化为第二套业务主路径。
 */

/**
 * @brief 按正式主控路径处理一行命令，并生成单行响应。
 * @param system_context 主控共享上下文，不能为空。
 * @param command_line 输入命令行，不能为空。
 * @param response_line 输出响应缓冲区，不能为空。
 * @param response_line_size 输出响应缓冲区大小，必须大于 0。
 * @return 命令执行结果；即使返回失败，也会尽量填充单行响应。
 *
 * @note 写命令会走“解析 -> 入队 -> 消费 -> 统一响应”路径。
 * @note `status` 会走“解析 -> 只读查询 -> 统一响应”路径，但仍受统一正式受理逻辑约束。
 */
operation_result_t cli_command_adapter_execute_formal_line(system_context_t system_context, const char *command_line,
                                                           char *response_line, size_t response_line_size);

#endif
