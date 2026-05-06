#include "application/use_cases/acknowledge_fault.h"

#include "application/use_cases/process_wash_trigger.h"
#include "domain/model/wash_trigger_event.h"

operation_result_t acknowledge_fault_execute(system_context_t *system_context,
    const char *fault_code,
    const char *fault_reason)
{
    wash_trigger_event_t wash_trigger_event;

    if (system_context == 0 || fault_code == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_FAULT,
        0,
        fault_code,
        fault_reason != 0 ? fault_reason : "fault-command",
        system_context->current_time_ms);
    return process_wash_trigger_execute(system_context, &wash_trigger_event);
}
