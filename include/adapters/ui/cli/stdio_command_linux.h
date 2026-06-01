#ifndef ADAPTERS_UI_CLI_STDIO_COMMAND_LINUX_H
#define ADAPTERS_UI_CLI_STDIO_COMMAND_LINUX_H

#include <stdbool.h>
#include <stddef.h>

#include "ports/application/inbound/command_port.h"
#include "core/scheduler/scheduler.h"
#include "shared/result_types.h"

/**
 * @file stdio_command_linux.h
 * @brief 声明 Linux 上基于 stdio 的命令输入适配器。
 *
 * @details 从字节流缓冲并切分完整命令文本，经 command_port_t 分发并将响应写回输出流，
 *          再封装为 command_source_port_t 供调度器注册。
 */

/**
 * @brief stdio 命令 IO 绑定。
 */
typedef struct stdio_command_io_t
{
    FILE *input;  /**< 命令输入流。 */
    FILE *output; /**< 命令响应输出流。 */
    FILE *error;  /**< 错误输出流（当前未使用，保留备用）。 */
} stdio_command_io_t;

/**
 * @brief Linux stdio 命令输入适配器状态。
 */
typedef struct stdio_command_linux_t
{
    stdio_command_io_t io;              /**< 标准输入输出绑定。 */
    int command_fd;                     /**< 已启用的命令输入 fd；未启用时为 -1。 */
    int command_fd_flags;               /**< 命令输入 fd 原始标志。 */
    bool command_fd_flags_valid;        /**< 原始 fd 标志是否有效。 */
    size_t command_buffer_length;       /**< 当前命令缓冲区有效字节数。 */
    bool command_input_eof;             /**< 命令输入流是否已到达 EOF。 */
    char command_buffer[512];           /**< 未消费的命令输入缓冲区。 */
} stdio_command_linux_t;

/**
 * @brief 初始化命令输入适配器。
 * @param cmd 适配器状态，不能为空。
 * @param io 可选标准输入输出绑定。
 */
void stdio_command_linux_init(stdio_command_linux_t *cmd, const stdio_command_io_t *io);

/**
 * @brief 启用命令输入非阻塞读取。
 * @param cmd 适配器状态，不能为空。
 * @return 启用成功返回命令 fd；未绑定输入或底层配置失败时返回 `-1`。
 */
int stdio_command_linux_enable(stdio_command_linux_t *cmd);

/**
 * @brief 恢复命令输入 fd 原始标志。
 * @param cmd 适配器状态，允许为 `0`。
 */
void stdio_command_linux_restore(stdio_command_linux_t *cmd);

/**
 * @brief 读取当前命令 fd。
 * @param cmd 适配器状态，允许为 `0`。
 * @return 当前命令 fd；未启用时返回 `-1`。
 */
int stdio_command_linux_fd(const stdio_command_linux_t *cmd);

/**
 * @brief 判断缓冲区中是否存在待处理的完整命令。
 * @param cmd 适配器状态，允许为 `0`。
 * @return 存在完整命令时返回 `true`。
 */
bool stdio_command_linux_has_buffered(const stdio_command_linux_t *cmd);

/**
 * @brief 判断 EOF 是否已到达且缓冲区已耗尽。
 * @param cmd 适配器状态，允许为 `0`。
 * @return EOF 到达且缓冲区为空时返回 `true`。
 */
bool stdio_command_linux_is_eof_drained(const stdio_command_linux_t *cmd);

/**
 * @brief 从 fd 读取数据、提取完整命令并通过端口分发。
 * @param cmd 适配器状态，不能为空。
 * @param command_port 命令处理入站端口，不能为空。
 * @param command_processed 输出：本次调用是否分发了一条命令。
 * @return 基础设施错误（IO 失败、缓冲区溢出）时返回失败；
 *         命令层错误已通过端口响应表达，本函数返回 `ok`。
 */
operation_result_t stdio_command_linux_handle_fd(stdio_command_linux_t *cmd,
                                                  const command_port_t *command_port,
                                                  bool *command_processed);

/**
 * @brief 将适配器封装为命令输入源端口，供调度器注册与调度使用。
 * @param cmd 已完成 enable 的适配器状态，不能为空。
 * @return 填充完毕的命令输入源端口；cmd 为空时返回 fd=-1 的空端口。
 * @note 返回的端口内部持有 cmd 指针，调用方须保证 cmd 生命周期长于端口使用期。
 */
command_source_port_t stdio_command_linux_as_source_port(stdio_command_linux_t *cmd);

#endif
