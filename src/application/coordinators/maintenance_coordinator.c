#include "application/coordinators/maintenance_coordinator.h"

#include "application/use_cases/acknowledge_fault.h"
#include "application/use_cases/enter_maintenance_mode.h"
#include "application/use_cases/jog_actuator.h"
#include "application/use_cases/update_program_config.h"

operation_result_t maintenance_coordinator_dispatch(system_context_t *system_context, const command_dto_t *command_dto)
{
    if (command_dto == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    switch (command_dto->command_type) {
    case COMMAND_TYPE_ACK_FAULT:
        return acknowledge_fault_execute(system_context, command_dto->target_id);
    case COMMAND_TYPE_ENTER_MAINTENANCE:
        return enter_maintenance_mode_execute(system_context);
    case COMMAND_TYPE_JOG:
        return jog_actuator_execute(system_context, command_dto);
    case COMMAND_TYPE_UPDATE_PROGRAM:
        return update_program_config_execute(system_context, command_dto->note);
    default:
        return operation_result_fail(ERROR_CODE_UNSUPPORTED);
    }
}

