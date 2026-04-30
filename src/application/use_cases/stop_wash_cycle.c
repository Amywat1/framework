#include "application/use_cases/stop_wash_cycle.h"

#include "domain/services/recovery_state_machine.h"

operation_result_t stop_wash_cycle_execute(system_context_t *system_context)
{
    operation_result_t result = recovery_state_machine_execute(&system_context->actuator_port);
    if (!result.ok) {
        return result;
    }
    system_context->wash_cycle.cycle_state = CYCLE_STATE_SAFE_STOP;
    system_context->wash_cycle.result_code = RESULT_CODE_MANUAL_ABORT;
    return operation_result_ok();
}

