#include "adapters/inbound/cli_command_adapter.h"

#include <string.h>

#include "application/use_cases/process_formal_command.h"
#include "shared/error_codes.h"

operation_result_t cli_command_adapter_prepare_line(system_context_t *system_context,
    const char *command_line,
    wash_trigger_event_t *wash_trigger_event,
    bool *has_trigger,
    char *response_line,
    size_t response_line_size)
{
    formal_command_request_t formal_command_request;
    operation_result_t result;

    if (system_context == 0 || command_line == 0 || wash_trigger_event == 0
        || has_trigger == 0 || response_line == 0 || response_line_size == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    result = process_formal_command_prepare_line(system_context,
        command_line,
        &formal_command_request,
        response_line,
        response_line_size);
    *has_trigger = formal_command_request.has_trigger;
    if (formal_command_request.has_trigger) {
        *wash_trigger_event = formal_command_request.wash_trigger_event;
    } else {
        memset(wash_trigger_event, 0, sizeof(*wash_trigger_event));
    }
    return result;
}

operation_result_t cli_command_adapter_finalize_trigger_response(system_context_t *system_context,
    operation_result_t result,
    char *response_line,
    size_t response_line_size)
{
    return process_formal_command_finalize_response(system_context, result, response_line, response_line_size);
}

operation_result_t cli_command_adapter_execute_formal_line(system_context_t *system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size)
{
    return process_formal_command_execute(system_context, command_line, response_line, response_line_size);
}

operation_result_t cli_command_adapter_process_line(system_context_t *system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size)
{
    return cli_command_adapter_execute_formal_line(system_context, command_line, response_line, response_line_size);
}
