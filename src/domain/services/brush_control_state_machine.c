#include "domain/services/brush_control_state_machine.h"

#include "shared/error_codes.h"
#include "shared/timeouts.h"

operation_result_t brush_control_state_machine_execute(const actuator_port_t *actuator_port, const wash_stage_t *wash_stage)
{
    if (actuator_port == 0 || wash_stage == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (actuator_port->control_roof_brush == 0 || actuator_port->control_side_brush == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (actuator_port->control_roof_brush(actuator_port->context, wash_stage->roof_brush_mode, BRUSH_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_TIMEOUT);
    }
    if (actuator_port->control_side_brush(actuator_port->context, wash_stage->side_brush_mode, BRUSH_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_TIMEOUT);
    }
    return operation_result_ok();
}

