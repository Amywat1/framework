#include "application/use_cases/update_program_config.h"

#include "adapters/config/json_program_parser.h"
#include "adapters/outbound/file_program_repository.h"

operation_result_t update_program_config_execute(system_context_t *system_context, const char *config_path)
{
    operation_result_t result;

    if (system_context == 0 || config_path == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    result = json_program_parser_parse(config_path, &system_context->wash_program);
    if (!result.ok) {
        return result;
    }
    if (system_context->wash_session.session_state != SESSION_STATE_RUNNING) {
        system_context->wash_program.revision += 1;
    }
    if (system_context->program_repository_port.save_program(system_context->program_repository_port.context, &system_context->wash_program) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    file_program_repository_set_runtime_program(system_context, &system_context->wash_program, system_context->wash_program.revision);
    return operation_result_ok();
}
