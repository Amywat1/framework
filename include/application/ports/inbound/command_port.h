#ifndef APPLICATION_PORTS_INBOUND_COMMAND_PORT_H
#define APPLICATION_PORTS_INBOUND_COMMAND_PORT_H

#include <stddef.h>

#include "shared/result_types.h"

/**
 * @file command_port.h
 * @brief 声明命令处理入站端口接口。
 *
 * @details 平台层 IO 适配器持有此端口，应用层通过实现此端口接收并处理命令行。
 *          依赖方向：平台层（调用方）→ 此端口接口 ← 应用层（实现方）。
 */

/**
 * @brief 命令处理入站端口。
 */
typedef struct command_port_t
{
    /**
     * @brief 处理一条命令行并生成响应。
     * @param context 实现侧上下文指针。
     * @param command_line 命令行文本，不能为空。
     * @param response_line 输出响应缓冲区，不能为空。
     * @param response_line_size 响应缓冲区大小，必须大于 0。
     * @return 基础设施级错误（tick 执行失败、缓冲区溢出等）返回失败；
     *         命令层错误（状态非法、解析失败等）通过响应文本表达并返回 `ok`。
     */
    operation_result_t (*handle)(void *context, const char *command_line,
                                  char *response_line, size_t response_line_size);
    void *context; /**< 实现侧上下文指针。 */
} command_port_t;

#endif
