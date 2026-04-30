#include "application/use_cases/enter_maintenance_mode.h"

operation_result_t enter_maintenance_mode_execute(system_context_t *system_context)
{
    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (system_context->event_logger_port.log_message != 0) {
        system_context->event_logger_port.log_message(system_context->event_logger_port.context, EVENT_TYPE_CYCLE_STATE_CHANGED, "进入维护模式");
    }
    return operation_result_ok();
}

