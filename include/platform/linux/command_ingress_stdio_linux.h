#ifndef PLATFORM_LINUX_COMMAND_INGRESS_STDIO_LINUX_H
#define PLATFORM_LINUX_COMMAND_INGRESS_STDIO_LINUX_H

#include <stdbool.h>
#include <stddef.h>

#include "application/ports/inbound/command_port.h"
#include "platform/scheduler.h"
#include "shared/result_types.h"

/**
 * @file command_ingress_stdio_linux.h
 * @brief 声明 Linux 上基于 stdio 的命令入站适配器。
 *
 * @details 从字节流缓冲并切分完整命令文本，经 command_port_t 分发并将响应写回输出流。
 *          传输无关的调度接口由 command_source_port_t 表达；本模块仅覆盖 stdin/stdout 实现。
 */

/**
 * @brief stdio 命令入站 IO 绑定。
 */
typedef struct command_ingress_stdio_binding_t
{
    FILE *input;  /**< 命令输入流。 */
    FILE *output; /**< 命令响应输出流。 */
    FILE *error;  /**< 错误输出流（当前未使用，保留备用）。 */
} command_ingress_stdio_binding_t;

/**
 * @brief Linux stdio 命令入站适配器状态。
 */
typedef struct command_ingress_stdio_linux_t
{
    command_ingress_stdio_binding_t io_binding; /**< 标准输入输出绑定。 */
    int command_fd;                             /**< 已启用的命令输入 fd；未启用时为 -1。 */
    int command_fd_flags;                       /**< 命令输入 fd 原始标志。 */
    bool command_fd_flags_valid;                /**< 原始 fd 标志是否有效。 */
    size_t command_buffer_length;               /**< 当前命令缓冲区有效字节数。 */
    bool command_input_eof;                     /**< 命令输入流是否已到达 EOF。 */
    char command_buffer[512];                   /**< 未消费的命令输入缓冲区。 */
} command_ingress_stdio_linux_t;

/**
 * @brief 初始化命令入站适配器。
 * @param ingress 适配器状态，不能为空。
 * @param io_binding 可选标准输入输出绑定。
 */
void command_ingress_stdio_linux_init(command_ingress_stdio_linux_t *ingress,
                                       const command_ingress_stdio_binding_t *io_binding);

/**
 * @brief 启用命令输入非阻塞读取。
 * @param ingress 适配器状态，不能为空。
 * @return 启用成功返回命令 fd；未绑定输入或底层配置失败时返回 `-1`。
 */
int command_ingress_stdio_linux_enable(command_ingress_stdio_linux_t *ingress);

/**
 * @brief 恢复命令输入 fd 原始标志。
 * @param ingress 适配器状态，允许为 `0`。
 */
void command_ingress_stdio_linux_restore(command_ingress_stdio_linux_t *ingress);

/**
 * @brief 读取当前命令 fd。
 * @param ingress 适配器状态，允许为 `0`。
 * @return 当前命令 fd；未启用时返回 `-1`。
 */
int command_ingress_stdio_linux_fd(const command_ingress_stdio_linux_t *ingress);

/**
 * @brief 判断缓冲区中是否存在待处理的完整命令。
 * @param ingress 适配器状态，允许为 `0`。
 * @return 存在完整命令时返回 `true`。
 */
bool command_ingress_stdio_linux_has_buffered_command(const command_ingress_stdio_linux_t *ingress);

/**
 * @brief 判断 EOF 是否已到达且缓冲区已耗尽。
 * @param ingress 适配器状态，允许为 `0`。
 * @return EOF 到达且缓冲区为空时返回 `true`。
 */
bool command_ingress_stdio_linux_is_eof_and_drained(const command_ingress_stdio_linux_t *ingress);

/**
 * @brief 从 fd 读取数据、提取完整命令并通过端口分发。
 * @param ingress 适配器状态，不能为空。
 * @param command_port 命令处理入站端口，不能为空。
 * @param command_processed 输出：本次调用是否分发了一条命令。
 * @return 基础设施错误（IO 失败、缓冲区溢出）时返回失败；
 *         命令层错误已通过端口响应表达，本函数返回 `ok`。
 */
operation_result_t command_ingress_stdio_linux_handle_fd(command_ingress_stdio_linux_t *ingress,
                                                          const command_port_t *command_port,
                                                          bool *command_processed);

/**
 * @brief 将适配器封装为命令输入源端口，供调度器注册与调度使用。
 * @param ingress 已完成 enable 的适配器状态，不能为空。
 * @return 填充完毕的命令输入源端口；ingress 为空时返回 fd=-1 的空端口。
 * @note 返回的端口内部持有 ingress 指针，调用方须保证 ingress 生命周期长于端口使用期。
 */
command_source_port_t command_ingress_stdio_linux_as_source_port(command_ingress_stdio_linux_t *ingress);

#endif
