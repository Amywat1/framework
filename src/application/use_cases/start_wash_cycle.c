#include "application/use_cases/start_wash_cycle.h"

#include "application/coordinators/wash_cycle_coordinator.h"

operation_result_t start_wash_cycle_execute(system_context_t *system_context, const command_dto_t *command_dto)
{
    if (command_dto == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    return wash_cycle_coordinator_run(system_context, command_dto->program_id);
}

