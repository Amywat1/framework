#include "domain/services/program_snapshot_service.h"

#include "shared/error_codes.h"

operation_result_t program_snapshot_service_capture(system_context_t *system_context, const char *program_id)
{
    int repository_result;

    if (system_context == 0 || program_id == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (system_context->program_repository_port.validate_program_snapshot == 0) {
        return operation_result_fail(ERROR_CODE_UNSUPPORTED);
    }

    repository_result = system_context->program_repository_port.validate_program_snapshot(
        system_context->program_repository_port.context,
        program_id,
        &system_context->program_snapshot,
        &system_context->wash_program);
    if (repository_result != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (system_context->program_snapshot.validation_result == PROGRAM_SNAPSHOT_VALIDATION_UNAVAILABLE) {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    if (system_context->program_snapshot.validation_result == PROGRAM_SNAPSHOT_VALIDATION_INVALID) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    program_snapshot_capture(&system_context->program_snapshot,
        program_id,
        system_context->wash_program.revision,
        system_context->current_time_ms,
        &system_context->wash_program,
        PROGRAM_SNAPSHOT_VALIDATION_VALID);
    return operation_result_ok();
}
