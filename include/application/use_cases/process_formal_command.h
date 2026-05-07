#ifndef APPLICATION_USE_CASES_PROCESS_FORMAL_COMMAND_H
#define APPLICATION_USE_CASES_PROCESS_FORMAL_COMMAND_H

#include <stdbool.h>
#include <stddef.h>

#include "application/coordinators/system_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file process_formal_command.h
 * @brief 定义统一正式命令受理入口。
 */

/**
 * @brief 描述一条正式命令在应用层入口中的受理结果。
 */
typedef struct formal_command_request_t {
    bool has_trigger;
    bool requires_queue;
    wash_trigger_event_t wash_trigger_event;
} formal_command_request_t;

/**
 * @brief 解析一行正式命令并构建统一受理请求。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param command_line 输入命令行，不能为空。
 * @param formal_command_request 输出统一受理请求，不能为空。
 * @param response_line 输出响应缓冲区，不能为空。
 * @param response_line_size 输出响应缓冲区大小，必须大于 0。
 * @return 解析成功返回 `operation_result_ok()`；解析失败时返回失败结果并尽量填充响应。
 *
 * @note `status` 命令会直接生成响应，并返回 `has_trigger=false`、`requires_queue=false`。
 */
operation_result_t process_formal_command_prepare_line(system_context_t *system_context,
    const char *command_line,
    formal_command_request_t *formal_command_request,
    char *response_line,
    size_t response_line_size);

/**
 * @brief 根据最近一次正式命令处理结果生成单行响应。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param result 最近一次命令处理返回值。
 * @param response_line 输出响应缓冲区，不能为空。
 * @param response_line_size 输出响应缓冲区大小，必须大于 0。
 * @return 原样返回 `result`。
 */
operation_result_t process_formal_command_finalize_response(system_context_t *system_context,
    operation_result_t result,
    char *response_line,
    size_t response_line_size);

/**
 * @brief 按统一正式受理路径处理一行命令并生成响应。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param command_line 输入命令行，不能为空。
 * @param response_line 输出响应缓冲区，不能为空。
 * @param response_line_size 输出响应缓冲区大小，必须大于 0。
 * @return 命令处理结果；即使返回失败，也会尽量填充单行响应。
 */
operation_result_t process_formal_command_execute(system_context_t *system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size);

#endif
