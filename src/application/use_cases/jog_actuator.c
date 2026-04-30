#include "application/use_cases/jog_actuator.h"

#include "shared/timeouts.h"

operation_result_t jog_actuator_execute(system_context_t *system_context, const command_dto_t *command_dto)
{
    int duration_ms;

    if (system_context == 0 || command_dto == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    duration_ms = command_dto->duration_ms > 0 ? command_dto->duration_ms : STOP_TIMEOUT_MS;
    if (system_context->actuator_port.move_gantry != 0) {
        if (system_context->actuator_port.move_gantry(system_context->actuator_port.context, GANTRY_MOTION_FORWARD, 1, duration_ms) != 0) {
            return operation_result_fail(ERROR_CODE_TIMEOUT);
        }
    }
    return operation_result_ok();
}

