#include "domain/model/program_validation.h"

#include <string.h>

#include "shared/error_codes.h"

operation_result_t program_validation_validate(const wash_program_t *wash_program)
{
    int index;
    int compare_index;

    if (wash_program == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (wash_program->segment_count <= 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    for (index = 0; index < wash_program->segment_count; ++index)
    {
        if (!wash_segment_is_valid(&wash_program->segments[index]))
        {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        if (wash_program->segments[index].sequence_no != index + 1)
        {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        for (compare_index = index + 1; compare_index < wash_program->segment_count; ++compare_index)
        {
            if (strcmp(wash_program->segments[index].segment_id, wash_program->segments[compare_index].segment_id) == 0)
            {
                return operation_result_fail(ERROR_CODE_PARSE_FAILED);
            }
        }
    }
    return operation_result_ok();
}
