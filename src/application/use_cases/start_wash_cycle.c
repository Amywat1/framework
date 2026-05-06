#include "application/use_cases/start_wash_cycle.h"

#include "application/use_cases/process_wash_trigger.h"
#include "domain/model/wash_trigger_event.h"

operation_result_t start_wash_cycle_execute(system_context_t *system_context, const command_dto_t *command_dto)
{
    wash_trigger_event_t wash_trigger_event;

    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (command_dto == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_START,
        command_dto->program_id,
        0,
        "start-command",
        system_context->current_time_ms);
    return process_wash_trigger_execute(system_context, &wash_trigger_event);
}
