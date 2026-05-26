#ifndef SRC_PLATFORM_LINUX_STDIO_FORMAL_COMMAND_ADAPTER_H
#define SRC_PLATFORM_LINUX_STDIO_FORMAL_COMMAND_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>

#include "platform/scheduler.h"
#include "shared/result_types.h"

/**
 * @file stdio_formal_command_adapter.h
 * @brief 声明 Linux 平台 stdin/stdout formal command 入站适配器。
 */

typedef struct stdio_formal_command_adapter_t
{
    scheduler_stdio_t stdio_binding; /**< 标准输入输出绑定。 */
    int command_fd;                             /**< 已启用的命令输入 fd；未启用时为 -1。 */
    int command_fd_flags;                       /**< 命令输入 fd 原始标志。 */
    bool command_fd_flags_valid;                /**< 原始 fd 标志是否有效。 */
    size_t command_buffer_length;               /**< 当前命令缓冲区有效字节数。 */
    bool command_input_eof;                     /**< 命令输入流是否已到达 EOF。 */
    char command_buffer[512];                   /**< 未消费的命令输入缓冲区。 */
} stdio_formal_command_adapter_t;

/**
 * @brief 初始化 stdin formal command 适配器。
 * @param adapter 适配器状态，不能为空。
 * @param stdio_binding 可选标准输入输出绑定。
 */
void stdio_formal_command_adapter_init(stdio_formal_command_adapter_t *adapter,
                                       const scheduler_stdio_t *stdio_binding);

/**
 * @brief 启用 stdin formal command 输入源。
 * @param adapter 适配器状态，不能为空。
 * @return 启用成功返回命令 fd；未绑定输入时返回 `-1`，底层配置失败时返回 `-1`。
 */
int stdio_formal_command_adapter_enable(stdio_formal_command_adapter_t *adapter);

/**
 * @brief 恢复 stdin 输入 fd 原始标志。
 * @param adapter 适配器状态，允许为 `0`。
 */
void stdio_formal_command_adapter_restore(stdio_formal_command_adapter_t *adapter);

/**
 * @brief 读取适配器当前命令 fd。
 * @param adapter 适配器状态，允许为 `0`。
 * @return 当前命令 fd；未启用时返回 `-1`。
 */
int stdio_formal_command_adapter_fd(const stdio_formal_command_adapter_t *adapter);

/**
 * @brief 从命令输入 fd 读取数据并处理完整 formal command 行。
 * @param adapter 适配器状态，不能为空。
 * @param scheduler 所属调度器实例，不能为空。
 * @param failpoint_command_read 是否注入命令读取失败。
 * @return 处理成功返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t stdio_formal_command_adapter_handle_fd(stdio_formal_command_adapter_t *adapter,
                                                          scheduler_t *scheduler,
                                                          bool failpoint_command_read);

/**
 * @brief 处理一条测试或外部注入的 formal command 行。
 * @param adapter 适配器状态，不能为空。
 * @param scheduler 所属调度器实例，不能为空。
 * @param command_line 命令行文本，不能为空。
 * @param response_line 输出响应缓冲区。
 * @param response_line_size 输出响应缓冲区大小。
 * @param print_response 是否写回 stdio 输出流。
 * @return 处理成功返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t stdio_formal_command_adapter_handle_command_event(
    stdio_formal_command_adapter_t *adapter, scheduler_t *scheduler, const char *command_line,
    char *response_line, size_t response_line_size, bool print_response);

#endif
