#include "application/use_cases/stop_wash_cycle.h"

#include "application/coordinators/compatibility_trigger_runner.h"
#include "domain/model/wash_trigger_event.h"

operation_result_t stop_wash_cycle_execute(system_context_t *system_context)
{
    return stop_wash_cycle_with_reason_execute(system_context, "manual-stop");
}

operation_result_t stop_wash_cycle_with_reason_execute(system_context_t *system_context, const char *reason_code)
{
    wash_trigger_event_t wash_trigger_event;

    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_STOP,
        0,
        reason_code,
        "stop-command",
        system_context->current_time_ms);
    return compatibility_trigger_runner_execute(system_context, &wash_trigger_event);
}
