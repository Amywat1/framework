#ifndef ADAPTERS_INBOUND_CLI_COMMAND_ADAPTER_H
#define ADAPTERS_INBOUND_CLI_COMMAND_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>

#include "application/coordinators/system_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file cli_command_adapter.h
 * @brief 定义主控命令行输入适配器。
 */

/**
 * @brief 解析一行正式主控命令。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param command_line 输入命令行，不能为空。
 * @param wash_trigger_event 输出触发事件；当 `has_trigger` 返回 `true` 时不能为空。
 * @param has_trigger 输出是否生成了待入队触发；不能为空。
 * @param response_line 输出响应缓冲区，不能为空。
 * @param response_line_size 输出响应缓冲区大小，必须大于 0。
 * @return 解析成功返回 `operation_result_ok()`；解析失败时返回失败结果并尽量填充响应。
 *
 * @note `status` 命令会直接生成响应并返回 `has_trigger=false`。
 * @note `start/stop/feedback/fault` 命令只负责解析触发，不直接推进业务状态。
 */
operation_result_t cli_command_adapter_prepare_line(system_context_t *system_context,
    const char *command_line,
    wash_trigger_event_t *wash_trigger_event,
    bool *has_trigger,
    char *response_line,
    size_t response_line_size);

/**
 * @brief 根据最近一次触发处理结果生成单行响应。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param result 最近一次触发处理返回值。
 * @param response_line 输出响应缓冲区，不能为空。
 * @param response_line_size 输出响应缓冲区大小，必须大于 0。
 * @return 原样返回 `result`。
 */
operation_result_t cli_command_adapter_finalize_trigger_response(system_context_t *system_context,
    operation_result_t result,
    char *response_line,
    size_t response_line_size);

/**
 * @brief 按正式主控路径处理一行命令，并生成单行响应。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param command_line 输入命令行，不能为空。
 * @param response_line 输出响应缓冲区，不能为空。
 * @param response_line_size 输出响应缓冲区大小，必须大于 0。
 * @return 命令执行结果；即使返回失败，也会尽量填充单行响应。
 *
 * @note 写命令会走“解析 -> 入队 -> 消费 -> 统一响应”路径。
 * @note `status` 会走“解析 -> 只读查询 -> 统一响应”路径，但仍受统一正式受理逻辑约束。
 */
operation_result_t cli_command_adapter_execute_formal_line(system_context_t *system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size);

/**
 * @brief 兼容旧调用方的一行命令处理包装。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param command_line 输入命令行，不能为空。
 * @param response_line 输出响应缓冲区，不能为空。
 * @param response_line_size 输出响应缓冲区大小，必须大于 0。
 * @return 命令执行结果；即使返回失败，也会尽量填充单行响应。
 *
 * @note 本接口保留为兼容性包装，内部复用 `cli_command_adapter_execute_formal_line()`。
 * @note 新代码应优先显式使用正式受理接口，避免把该兼容名当作另一条主路径。
 */
operation_result_t cli_command_adapter_process_line(system_context_t *system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size);

#endif
