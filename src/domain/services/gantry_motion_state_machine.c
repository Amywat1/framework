#include "domain/services/gantry_motion_state_machine.h"

#include "shared/error_codes.h"
#include "shared/timeouts.h"

operation_result_t gantry_motion_state_machine_execute(const actuator_port_t *actuator_port, const wash_stage_t *wash_stage)
{
    if (actuator_port == 0 || wash_stage == 0 || actuator_port->move_gantry == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (actuator_port->move_gantry(actuator_port->context, wash_stage->gantry_motion_mode, wash_stage->traverse_count, MOTION_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_TIMEOUT);
    }
    return operation_result_ok();
}

