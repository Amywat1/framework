#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "src/platform/linux/stdio_formal_command_adapter.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "application/use_cases/process_formal_command.h"
#include "shared/error_codes.h"
#include "src/platform/linux/scheduler_linux_internal.h"

/**
 * @brief 根据最新上下文结果重建 formal command 响应行。
 * @param device_runtime 系统上下文句柄。
 * @param response_line 输出响应缓冲区。
 * @param response_line_size 输出缓冲区大小。
 */
static void stdio_formal_command_adapter_rebuild_response(const device_runtime_t device_runtime, char *response_line,
                                                          size_t response_line_size)
{
    const char *detail;
    const char *result_code;

    if (response_line == 0 || response_line_size == 0u)
    {
        return;
    }

    result_code = device_runtime_last_result_code(device_runtime)[0] != '\0'
                      ? device_runtime_last_result_code(device_runtime)
                      : "accepted";
    detail = device_runtime_last_reason_code(device_runtime)[0] != '\0'
                 ? device_runtime_last_reason_code(device_runtime)
                 : "none";
    process_formal_command_format_response(response_line, response_line_size, result_code,
                                           process_formal_command_result_is_accepted(result_code), detail);
}

/**
 * @brief 判断命令缓冲区中是否已有完整命令行。
 * @param adapter 适配器状态。
 * @return 存在完整命令行时返回 `true`。
 */
static bool stdio_formal_command_adapter_has_complete_line(const stdio_formal_command_adapter_t *adapter)
{
    if (adapter == 0)
    {
        return false;
    }
    if (memchr(adapter->command_buffer, '\n', adapter->command_buffer_length) != 0)
    {
        return true;
    }
    return adapter->command_input_eof && adapter->command_buffer_length > 0u;
}

/**
 * @brief 将新读取的命令字节追加到命令缓冲区。
 * @param adapter 适配器状态。
 * @param scheduler 所属调度器实例。
 * @param bytes 待读取的字节序列。
 * @param byte_count 字节数。
 * @return 追加成功返回 `operation_result_ok()`。
 */
static operation_result_t stdio_formal_command_adapter_append_bytes(stdio_formal_command_adapter_t *adapter,
                                                                    scheduler_t *scheduler,
                                                                    const char *bytes, size_t byte_count)
{
    if (adapter == 0 || scheduler == 0 || (bytes == 0 && byte_count > 0u))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (adapter->command_buffer_length + byte_count >= sizeof(adapter->command_buffer))
    {
        scheduler_record_error(scheduler, "command_line_too_long", true);
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    if (byte_count > 0u)
    {
        memcpy(adapter->command_buffer + adapter->command_buffer_length, bytes, byte_count);
        adapter->command_buffer_length += byte_count;
        adapter->command_buffer[adapter->command_buffer_length] = '\0';
    }
    return operation_result_ok();
}

/**
 * @brief 从命令缓冲区提取一行命令并消费对应字节。
 * @param adapter 适配器状态。
 * @param command_line 输出命令行缓冲区。
 * @param command_line_size 输出缓冲区大小。
 * @return 提取到完整命令行时返回 `true`。
 */
static bool stdio_formal_command_adapter_extract_line(stdio_formal_command_adapter_t *adapter, char *command_line,
                                                      size_t command_line_size)
{
    char *newline_ptr;
    size_t line_length;
    size_t consume_length;

    if (adapter == 0 || command_line == 0 || command_line_size == 0u)
    {
        return false;
    }
    if (!stdio_formal_command_adapter_has_complete_line(adapter))
    {
        return false;
    }

    newline_ptr = (char *)memchr(adapter->command_buffer, '\n', adapter->command_buffer_length);
    if (newline_ptr != 0)
    {
        line_length = (size_t)(newline_ptr - adapter->command_buffer);
        consume_length = line_length + 1u;
    }
    else
    {
        line_length = adapter->command_buffer_length;
        consume_length = line_length;
    }
    if (line_length > 0u && adapter->command_buffer[line_length - 1u] == '\r')
    {
        line_length -= 1u;
    }
    if (line_length >= command_line_size)
    {
        line_length = command_line_size - 1u;
    }
    memcpy(command_line, adapter->command_buffer, line_length);
    command_line[line_length] = '\0';

    if (consume_length < adapter->command_buffer_length)
    {
        memmove(adapter->command_buffer, adapter->command_buffer + consume_length,
                adapter->command_buffer_length - consume_length);
    }
    adapter->command_buffer_length -= consume_length;
    adapter->command_buffer[adapter->command_buffer_length] = '\0';
    return true;
}

/**
 * @brief 执行一条 formal command 命令行并按需回填响应。
 * @param adapter 适配器状态。
 * @param scheduler 所属调度器实例。
 * @param command_line 输入命令行。
 * @param response_line 输出响应缓冲区。
 * @param response_line_size 输出缓冲区大小。
 * @param print_response 是否将响应写回标准输出。
 * @return 处理成功返回 `operation_result_ok()`。
 */
static operation_result_t stdio_formal_command_adapter_process_line(
    stdio_formal_command_adapter_t *adapter, scheduler_t *scheduler, const char *command_line,
    char *response_line, size_t response_line_size, bool print_response)
{
    char local_response[512];
    operation_result_t result;
    unsigned int pending_before;
    bool queued_work;

    if (adapter == 0 || scheduler == 0 || command_line == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (response_line == 0 || response_line_size == 0u)
    {
        response_line = local_response;
        response_line_size = sizeof(local_response);
    }
    memset(response_line, 0, response_line_size);

    /* runtime_port.context 始终由 scheduler_runtime_port_init_from_device_runtime 写入 device_runtime_t */
    device_runtime_t device_runtime = (device_runtime_t)scheduler->runtime_port.context;
    pending_before =
        scheduler->runtime_port.pending_trigger_count(scheduler->runtime_port.context);
    result = process_formal_command_execute(device_runtime, command_line, response_line, response_line_size);
    queued_work =
        scheduler->runtime_port.pending_trigger_count(scheduler->runtime_port.context) >
        pending_before;
    if (result.ok && queued_work &&
        scheduler->runtime_state == SCHEDULER_RUNTIME_STATE_RUNNING)
    {
        result = scheduler_execute_bounded_ticks(scheduler, false, 0ul, false, 0ul);
        if (!result.ok)
        {
            return result;
        }
        stdio_formal_command_adapter_rebuild_response(device_runtime, response_line, response_line_size);
    }
    if (!result.ok && response_line[0] == '\0')
    {
        scheduler_record_error(scheduler, "command_dispatch_failed", true);
        return result;
    }
    if (print_response && adapter->stdio_binding.output != 0 && response_line[0] != '\0')
    {
        fprintf(adapter->stdio_binding.output, "%s\n", response_line);
        fflush(adapter->stdio_binding.output);
    }
    scheduler_update_pending_metric(scheduler);
    return operation_result_ok();
}

void stdio_formal_command_adapter_init(stdio_formal_command_adapter_t *adapter,
                                       const scheduler_stdio_t *stdio_binding)
{
    if (adapter == 0)
    {
        return;
    }

    memset(adapter, 0, sizeof(*adapter));
    adapter->command_fd = -1;
    if (stdio_binding != 0)
    {
        adapter->stdio_binding = *stdio_binding;
    }
}

int stdio_formal_command_adapter_enable(stdio_formal_command_adapter_t *adapter)
{
    if (adapter == 0 || adapter->stdio_binding.input == 0)
    {
        return -1;
    }

    adapter->command_fd = fileno(adapter->stdio_binding.input);
    if (adapter->command_fd < 0)
    {
        return -1;
    }

    adapter->command_fd_flags = fcntl(adapter->command_fd, F_GETFL, 0);
    adapter->command_fd_flags_valid = adapter->command_fd_flags >= 0;
    if (adapter->command_fd_flags_valid &&
        fcntl(adapter->command_fd, F_SETFL, adapter->command_fd_flags | O_NONBLOCK) < 0)
    {
        return -1;
    }
    return adapter->command_fd;
}

void stdio_formal_command_adapter_restore(stdio_formal_command_adapter_t *adapter)
{
    if (adapter == 0)
    {
        return;
    }
    if (adapter->command_fd >= 0 && adapter->command_fd_flags_valid)
    {
        fcntl(adapter->command_fd, F_SETFL, adapter->command_fd_flags);
    }
}

int stdio_formal_command_adapter_fd(const stdio_formal_command_adapter_t *adapter)
{
    return adapter != 0 ? adapter->command_fd : -1;
}

operation_result_t stdio_formal_command_adapter_handle_fd(stdio_formal_command_adapter_t *adapter,
                                                          scheduler_t *scheduler,
                                                          bool failpoint_command_read)
{
    char read_buffer[128];
    char command_line[256];
    ssize_t read_count;
    operation_result_t result;

    if (adapter == 0 || scheduler == 0 || adapter->command_fd < 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (stdio_formal_command_adapter_extract_line(adapter, command_line, sizeof(command_line)))
    {
        scheduler->pending_command_event = stdio_formal_command_adapter_has_complete_line(adapter);
        result = stdio_formal_command_adapter_handle_command_event(adapter, scheduler, command_line, 0, 0u,
                                                                   true);
        if (!result.ok)
        {
            return result;
        }
        if (adapter->command_input_eof && adapter->command_buffer_length == 0u)
        {
            return scheduler_request_exit_internal(scheduler, false, false);
        }
        return operation_result_ok();
    }

    scheduler->pending_command_event = false;
    if (failpoint_command_read)
    {
        scheduler_record_error(scheduler, "command_read_failed", true);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    for (;;)
    {
        read_count = read(adapter->command_fd, read_buffer, sizeof(read_buffer));
        if (read_count > 0)
        {
            result = stdio_formal_command_adapter_append_bytes(adapter, scheduler, read_buffer,
                                                               (size_t)read_count);
            if (!result.ok)
            {
                return result;
            }
            continue;
        }
        if (read_count == 0)
        {
            adapter->command_input_eof = true;
            scheduler->command_source.source_state = SCHEDULER_EVENT_SOURCE_CLOSED;
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }
        scheduler_record_error(scheduler, "command_read_failed", true);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    if (stdio_formal_command_adapter_extract_line(adapter, command_line, sizeof(command_line)))
    {
        scheduler->pending_command_event = stdio_formal_command_adapter_has_complete_line(adapter);
        result = stdio_formal_command_adapter_handle_command_event(adapter, scheduler, command_line, 0, 0u,
                                                                   true);
        if (!result.ok)
        {
            return result;
        }
    }
    if (adapter->command_input_eof && adapter->command_buffer_length == 0u)
    {
        return scheduler_request_exit_internal(scheduler, false, false);
    }
    return operation_result_ok();
}

operation_result_t stdio_formal_command_adapter_handle_command_event(
    stdio_formal_command_adapter_t *adapter, scheduler_t *scheduler, const char *command_line,
    char *response_line, size_t response_line_size, bool print_response)
{
    if (adapter == 0 || scheduler == 0 || command_line == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    scheduler_note_command_event(scheduler, command_line);
    return stdio_formal_command_adapter_process_line(adapter, scheduler, command_line, response_line,
                                                     response_line_size, print_response);
}
