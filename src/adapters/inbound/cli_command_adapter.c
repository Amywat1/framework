#include "adapters/inbound/cli_command_adapter.h"

#include <string.h>

#include "application/use_cases/process_formal_command.h"

operation_result_t cli_command_adapter_execute_formal_line(system_context_t system_context, const char *command_line,
                                                           char *response_line, size_t response_line_size)
{
    return process_formal_command_execute(system_context, command_line, response_line, response_line_size);
}
