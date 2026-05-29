#include "application/services/command_dispatch.h"

#include <string.h>

#include "application/coordinators/control_context.h"
#include "application/use_cases/formal_command.h"
#include "shared/error_codes.h"

/**
 * @brief command_port_t 的实现函数：处理一条命令行，按需同步推进 tick 并重建响应。
 * @param context command_dispatch_t 实例指针。
 * @param command_line 命令行文本。
 * @param response_line 输出响应缓冲区。
 * @param response_line_size 响应缓冲区大小。
 * @return tick 执行失败时返回对应错误；命令层错误通过响应文本表达并返回 `ok`。
 */
static operation_result_t command_dispatch_handle(void *context, const char *command_line, char *response_line,
                                                   size_t response_line_size)
{
    command_dispatch_t *dispatch;
    unsigned int pending_before;
    operation_result_t result;
    const char *result_code;
    const char *detail;

    dispatch = (command_dispatch_t *)context;
    if (dispatch == 0 || command_line == 0 || response_line == 0 || response_line_size == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    pending_before = dispatch->scheduler_sync_port.pending_trigger_count(dispatch->scheduler_sync_port.context);

    result = formal_command_execute(command_line, response_line, response_line_size);

    if (result.ok &&
        dispatch->scheduler_sync_port.pending_trigger_count(dispatch->scheduler_sync_port.context) > pending_before &&
        dispatch->scheduler_sync_port.is_running(dispatch->scheduler_sync_port.context))
    {
        result = dispatch->scheduler_sync_port.execute_bounded_ticks(dispatch->scheduler_sync_port.context);
        if (!result.ok)
        {
            return result;
        }
        result_code = control_context_last_result_code()[0] != '\0' ? control_context_last_result_code() : "accepted";
        detail = control_context_last_reason_code()[0] != '\0' ? control_context_last_reason_code() : "none";
        formal_command_format_response(response_line, response_line_size, result_code,
                                               formal_command_result_is_accepted(result_code), detail);
        return operation_result_ok();
    }

    return operation_result_ok();
}

void command_dispatch_init(command_dispatch_t *dispatch, const scheduler_sync_port_t *scheduler_sync_port)
{
    if (dispatch == 0)
    {
        return;
    }
    memset(dispatch, 0, sizeof(*dispatch));
    if (scheduler_sync_port != 0)
    {
        dispatch->scheduler_sync_port = *scheduler_sync_port;
    }
}

command_port_t command_dispatch_as_port(command_dispatch_t *dispatch)
{
    command_port_t port;

    memset(&port, 0, sizeof(port));
    if (dispatch != 0)
    {
        port.handle = command_dispatch_handle;
        port.context = dispatch;
    }
    return port;
}
