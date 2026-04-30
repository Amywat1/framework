#include "application/use_cases/acknowledge_fault.h"

operation_result_t acknowledge_fault_execute(system_context_t *system_context, const char *event_id)
{
    if (system_context == 0 || event_id == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (system_context->event_logger_port.log_message != 0) {
        system_context->event_logger_port.log_message(system_context->event_logger_port.context, EVENT_TYPE_FAULT_CLEARED, "故障已确认");
    }
    return operation_result_ok();
}

