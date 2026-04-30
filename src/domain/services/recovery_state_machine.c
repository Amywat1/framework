#include "domain/services/recovery_state_machine.h"

#include "shared/error_codes.h"
#include "shared/timeouts.h"

operation_result_t recovery_state_machine_execute(const actuator_port_t *actuator_port)
{
    if (actuator_port == 0 || actuator_port->stop_all == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (actuator_port->stop_all(actuator_port->context, STOP_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_TIMEOUT);
    }
    return operation_result_ok();
}

