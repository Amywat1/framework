#ifndef APPLICATION_USE_CASES_PROCESS_FORMAL_COMMAND_H
#define APPLICATION_USE_CASES_PROCESS_FORMAL_COMMAND_H

#include <stddef.h>

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

/**
 * @file process_formal_command.h
 * @brief Defines the single formal command entry for controller command handling.
 */

operation_result_t process_formal_command_execute(system_context_t system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size);

#endif
