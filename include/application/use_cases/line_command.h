#ifndef APPLICATION_USE_CASES_LINE_COMMAND_H
#define APPLICATION_USE_CASES_LINE_COMMAND_H

#include <stdbool.h>
#include <stddef.h>

#include "core/runtime/control_context.h"
#include "shared/result_types.h"

/**
 * @file line_command.h
 * @brief 声明行文本命令协议的应用层入口。
 *
 * @details 语义见《设备状态与命令契约》；传输层（stdio、串口、TCP 等）无关。
 */

/**
 * @brief 判断行命令结果码是否表示已接受。
 * @param result_code 结果码；允许为 `0`。
 * @return 结果不是 `ignored`、`rejected` 或 `error` 时返回 `true`。
 */
bool line_command_result_is_accepted(const char *result_code);

/**
 * @brief 格式化行命令单行响应。
 * @param response_line 输出响应缓冲区。
 * @param response_line_size 输出响应缓冲区大小。
 * @param result_code 结果码；为空时写入 `unknown`。
 * @param accepted 是否受理。
 * @param detail 结果详情；为空时写入 `none`。
 */
void line_command_format_response(char *response_line, size_t response_line_size,
                                  const char *result_code, bool accepted, const char *detail);

/**
 * @brief 根据控制上下文最近结果重建行命令响应。
 * @param response_line 输出响应缓冲区。
 * @param response_line_size 输出响应缓冲区大小。
 */
void line_command_format_response_from_context(char *response_line, size_t response_line_size);

/**
 * @brief 执行一条行文本命令并生成单行响应。
 * @param command_line 输入命令行，不能为空。
 * @param response_line 输出响应缓冲区，不能为空。
 * @param response_line_size 输出响应缓冲区大小，必须大于 0。
 * @return 命令受理成功返回 `operation_result_ok()`；参数非法或状态不允许时返回失败结果。
 */
operation_result_t line_command_execute(const char *command_line,
                                        char *response_line, size_t response_line_size);

#endif
