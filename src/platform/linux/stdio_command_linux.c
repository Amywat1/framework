#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "platform/linux/stdio_command_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "shared/error_codes.h"

/**
 * @brief 判断缓冲区中是否存在完整命令文本。
 */
static bool has_complete_command(const stdio_command_linux_t *cmd)
{
    if (memchr(cmd->command_buffer, '\n', cmd->command_buffer_length) != 0)
    {
        return true;
    }
    return cmd->command_input_eof && cmd->command_buffer_length > 0u;
}

/**
 * @brief 将新读取的字节追加到命令缓冲区。
 * @param cmd 适配器状态。
 * @param bytes 待追加字节。
 * @param byte_count 字节数。
 * @return 缓冲区溢出时返回 `RESOURCE_UNAVAILABLE`；否则返回 `ok`。
 */
static operation_result_t append_bytes(stdio_command_linux_t *cmd, const char *bytes, size_t byte_count)
{
    if (cmd->command_buffer_length + byte_count >= sizeof(cmd->command_buffer))
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    if (byte_count > 0u)
    {
        memcpy(cmd->command_buffer + cmd->command_buffer_length, bytes, byte_count);
        cmd->command_buffer_length += byte_count;
        cmd->command_buffer[cmd->command_buffer_length] = '\0';
    }
    return operation_result_ok();
}

/**
 * @brief 从缓冲区提取一条完整命令并消费对应字节。
 * @param cmd 适配器状态。
 * @param command_text 输出命令文本缓冲区。
 * @param command_text_size 缓冲区大小。
 * @return 提取到完整命令时返回 `true`。
 */
static bool extract_command(stdio_command_linux_t *cmd, char *command_text, size_t command_text_size)
{
    char *newline_ptr;
    size_t command_length;
    size_t consume_length;

    if (!has_complete_command(cmd))
    {
        return false;
    }

    newline_ptr = (char *)memchr(cmd->command_buffer, '\n', cmd->command_buffer_length);
    if (newline_ptr != 0)
    {
        command_length = (size_t)(newline_ptr - cmd->command_buffer);
        consume_length = command_length + 1u;
    }
    else
    {
        command_length = cmd->command_buffer_length;
        consume_length = command_length;
    }
    if (command_length > 0u && cmd->command_buffer[command_length - 1u] == '\r')
    {
        command_length -= 1u;
    }
    if (command_length >= command_text_size)
    {
        command_length = command_text_size - 1u;
    }
    memcpy(command_text, cmd->command_buffer, command_length);
    command_text[command_length] = '\0';

    if (consume_length < cmd->command_buffer_length)
    {
        memmove(cmd->command_buffer, cmd->command_buffer + consume_length,
                cmd->command_buffer_length - consume_length);
    }
    cmd->command_buffer_length -= consume_length;
    cmd->command_buffer[cmd->command_buffer_length] = '\0';
    return true;
}

/**
 * @brief 调用命令端口处理一条命令，并将响应写回输出流。
 * @param cmd 适配器状态。
 * @param command_port 命令处理端口。
 * @param command_text 待处理的命令文本。
 * @return 端口返回的处理结果。
 */
static operation_result_t dispatch_command(stdio_command_linux_t *cmd, const command_port_t *command_port,
                                           const char *command_text)
{
    char response_line[512];
    operation_result_t result;

    memset(response_line, 0, sizeof(response_line));
    result = command_port->handle(command_port->context, command_text, response_line, sizeof(response_line));
    if (cmd->io.output != 0 && response_line[0] != '\0')
    {
        fprintf(cmd->io.output, "%s\n", response_line);
        fflush(cmd->io.output);
    }
    return result;
}

void stdio_command_linux_init(stdio_command_linux_t *cmd, const stdio_command_io_t *io)
{
    if (cmd == 0)
    {
        return;
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->command_fd = -1;
    if (io != 0)
    {
        cmd->io = *io;
    }
}

int stdio_command_linux_enable(stdio_command_linux_t *cmd)
{
    if (cmd == 0 || cmd->io.input == 0)
    {
        return -1;
    }
    cmd->command_fd = fileno(cmd->io.input);
    if (cmd->command_fd < 0)
    {
        return -1;
    }
    cmd->command_fd_flags = fcntl(cmd->command_fd, F_GETFL, 0);
    cmd->command_fd_flags_valid = cmd->command_fd_flags >= 0;
    if (cmd->command_fd_flags_valid && fcntl(cmd->command_fd, F_SETFL, cmd->command_fd_flags | O_NONBLOCK) < 0)
    {
        return -1;
    }
    return cmd->command_fd;
}

void stdio_command_linux_restore(stdio_command_linux_t *cmd)
{
    if (cmd == 0)
    {
        return;
    }
    if (cmd->command_fd >= 0 && cmd->command_fd_flags_valid)
    {
        fcntl(cmd->command_fd, F_SETFL, cmd->command_fd_flags);
        cmd->command_fd_flags_valid = false;
    }
}

int stdio_command_linux_fd(const stdio_command_linux_t *cmd)
{
    return cmd != 0 ? cmd->command_fd : -1;
}

bool stdio_command_linux_has_buffered(const stdio_command_linux_t *cmd)
{
    return cmd != 0 && has_complete_command(cmd);
}

bool stdio_command_linux_is_eof_drained(const stdio_command_linux_t *cmd)
{
    return cmd != 0 && cmd->command_input_eof && cmd->command_buffer_length == 0u;
}

operation_result_t stdio_command_linux_handle_fd(stdio_command_linux_t *cmd, const command_port_t *command_port,
                                                 bool *command_processed)
{
    char read_buffer[128];
    char command_text[256];
    ssize_t read_count;
    operation_result_t result;

    if (cmd == 0 || command_port == 0 || command_port->handle == 0 || cmd->command_fd < 0 || command_processed == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    *command_processed = false;

    if (extract_command(cmd, command_text, sizeof(command_text)))
    {
        *command_processed = true;
        return dispatch_command(cmd, command_port, command_text);
    }

    for (;;)
    {
        read_count = read(cmd->command_fd, read_buffer, sizeof(read_buffer));
        if (read_count > 0)
        {
            result = append_bytes(cmd, read_buffer, (size_t)read_count);
            if (!result.ok)
            {
                return result;
            }
            continue;
        }
        if (read_count == 0)
        {
            cmd->command_input_eof = true;
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    if (extract_command(cmd, command_text, sizeof(command_text)))
    {
        *command_processed = true;
        return dispatch_command(cmd, command_port, command_text);
    }

    return operation_result_ok();
}

static operation_result_t source_port_on_readable(void *ctx, const command_port_t *cmd_port, bool *command_processed)
{
    return stdio_command_linux_handle_fd((stdio_command_linux_t *)ctx, cmd_port, command_processed);
}

static bool source_port_has_buffered_data(void *ctx)
{
    return stdio_command_linux_has_buffered((stdio_command_linux_t *)ctx);
}

static bool source_port_is_eof_and_drained(void *ctx)
{
    return stdio_command_linux_is_eof_drained((stdio_command_linux_t *)ctx);
}

static void source_port_restore(void *ctx)
{
    stdio_command_linux_restore((stdio_command_linux_t *)ctx);
}

command_source_port_t stdio_command_linux_as_source_port(stdio_command_linux_t *cmd)
{
    command_source_port_t port;

    memset(&port, 0, sizeof(port));
    port.fd = -1;
    if (cmd != 0)
    {
        port.fd = cmd->command_fd;
        port.on_readable = source_port_on_readable;
        port.has_buffered_data = source_port_has_buffered_data;
        port.is_eof_and_drained = source_port_is_eof_and_drained;
        port.restore = source_port_restore;
        port.context = cmd;
    }
    return port;
}
