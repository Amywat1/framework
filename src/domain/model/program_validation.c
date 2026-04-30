#include "domain/model/program_validation.h"

#include "shared/error_codes.h"

operation_result_t program_validation_validate(const wash_program_t *wash_program)
{
    int index;

    if (wash_program == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (wash_program->stage_count <= 0) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    for (index = 0; index < wash_program->stage_count; ++index) {
        const wash_stage_t *wash_stage = &wash_program->stages[index];
        if (wash_stage->stage_timeout_ms <= 0) {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        if (wash_stage->gantry_motion_mode == GANTRY_MOTION_TRAVERSE && wash_stage->traverse_count <= 0) {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
    }
    return operation_result_ok();
}

