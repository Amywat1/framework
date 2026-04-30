#include "domain/services/chemical_dosing_state_machine.h"

#include "shared/error_codes.h"
#include "shared/timeouts.h"

operation_result_t chemical_dosing_state_machine_execute(const actuator_port_t *actuator_port, const wash_stage_t *wash_stage)
{
    int index;

    if (actuator_port == 0 || wash_stage == 0 || actuator_port->dose_chemical == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    for (index = 0; index < wash_stage->chemical_action_count; ++index) {
        const chemical_action_t *chemical_action = &wash_stage->chemical_actions[index];
        if (actuator_port->dose_chemical(actuator_port->context, chemical_action->channel_id, chemical_action->duration_ms, chemical_action->retry_limit, CHEMICAL_TIMEOUT_MS) != 0) {
            return operation_result_fail(ERROR_CODE_TIMEOUT);
        }
    }
    return operation_result_ok();
}

