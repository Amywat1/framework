#ifndef APPLICATION_USE_CASES_PROCESS_FORMAL_COMMAND_H
#define APPLICATION_USE_CASES_PROCESS_FORMAL_COMMAND_H

#include <stdbool.h>
#include <stddef.h>

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

/**
 * @file process_formal_command.h
 * @brief 声明主控正式命令的唯一应用层入口。
 */

/**
 * @brief 判断正式命令结果码是否表示已接受。
 * @param result_code 结果码；允许为 `0`。
 * @return 结果不是 `ignored`、`rejected` 或 `error` 时返回 `true`。
 */
bool process_formal_command_result_is_accepted(const char *result_code);

/**
 * @brief 格式化正式命令单行响应。
 * @param response_line 输出响应缓冲区。
 * @param response_line_size 输出响应缓冲区大小。
 * @param result_code 结果码；为空时写入 `unknown`。
 * @param accepted 是否受理。
 * @param detail 结果详情；为空时写入 `none`。
 */
void process_formal_command_format_response(char *response_line, size_t response_line_size,
                                            const char *result_code, bool accepted, const char *detail);

/**
 * @brief 执行一条正式命令并生成单行响应。
 * @param system_context 主控运行时组合根句柄。
 * @param command_line 输入命令行，不能为空。
 * @param response_line 输出响应缓冲区，不能为空。
 * @param response_line_size 输出响应缓冲区大小，必须大于 0。
 * @return 命令受理成功返回 `operation_result_ok()`；参数非法或状态不允许时返回失败结果。
 */
operation_result_t process_formal_command_execute(system_context_t system_context, const char *command_line,
                                                  char *response_line, size_t response_line_size);

#endif
