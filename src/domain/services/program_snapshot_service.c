#include "domain/services/program_snapshot_service.h"

#include "shared/error_codes.h"

operation_result_t program_snapshot_service_capture(program_snapshot_service_args_t *program_snapshot_service_args,
    const char *program_id)
{
    int repository_result;

    if (program_snapshot_service_args == 0
        || program_snapshot_service_args->program_snapshot == 0
        || program_snapshot_service_args->wash_program == 0
        || program_snapshot_service_args->program_repository_port == 0
        || program_id == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (program_snapshot_service_args->program_repository_port->validate_program_snapshot == 0) {
        return operation_result_fail(ERROR_CODE_UNSUPPORTED);
    }

    repository_result = program_snapshot_service_args->program_repository_port->validate_program_snapshot(
        program_snapshot_service_args->program_repository_port->context,
        program_id,
        program_snapshot_service_args->program_snapshot,
        program_snapshot_service_args->wash_program);
    if (repository_result != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (program_snapshot_service_args->program_snapshot->validation_result == PROGRAM_SNAPSHOT_VALIDATION_UNAVAILABLE) {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    if (program_snapshot_service_args->program_snapshot->validation_result == PROGRAM_SNAPSHOT_VALIDATION_INVALID) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    program_snapshot_capture(program_snapshot_service_args->program_snapshot,
        program_id,
        program_snapshot_service_args->wash_program->revision,
        program_snapshot_service_args->current_time_ms,
        program_snapshot_service_args->wash_program,
        PROGRAM_SNAPSHOT_VALIDATION_VALID);
    return operation_result_ok();
}
