#include "application/coordinators/wash_cycle_coordinator.h"

#include <string.h>

#include "application/use_cases/process_wash_trigger.h"
#include "domain/model/wash_trigger_event.h"
#include "domain/services/wait_timeout_service.h"

static void sync_legacy_cycle(system_context_t *system_context)
{
    if (system_context == 0) {
        return;
    }
    if (system_context->wash_session.session_id[0] != '\0') {
        strncpy(system_context->wash_cycle.cycle_id,
            system_context->wash_session.session_id,
            sizeof(system_context->wash_cycle.cycle_id) - 1);
        strncpy(system_context->wash_cycle.selected_program_id,
            system_context->wash_session.selected_program_id,
            sizeof(system_context->wash_cycle.selected_program_id) - 1);
        system_context->wash_cycle.current_stage_index = system_context->wash_execution.stage_index;
        system_context->wash_cycle.result_code = system_context->wash_session.final_session_result;
    }

    switch (system_context->wash_session.session_state) {
        case SESSION_STATE_CREATED:
            system_context->wash_cycle.cycle_state = CYCLE_STATE_PRECHECK;
            break;
        case SESSION_STATE_RUNNING:
            system_context->wash_cycle.cycle_state = CYCLE_STATE_RUNNING;
            break;
        case SESSION_STATE_COMPLETED:
            system_context->wash_cycle.cycle_state = CYCLE_STATE_COMPLETED;
            break;
        case SESSION_STATE_ABORTED:
            system_context->wash_cycle.cycle_state = CYCLE_STATE_ABORTED;
            break;
        default:
            system_context->wash_cycle.cycle_state = CYCLE_STATE_IDLE;
            break;
    }
}

operation_result_t wash_cycle_coordinator_submit(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    result = process_wash_trigger_execute(system_context, wash_trigger_event);
    sync_legacy_cycle(system_context);
    return result;
}

operation_result_t wash_cycle_coordinator_run(system_context_t *system_context, const char *program_id)
{
    wash_trigger_event_t wash_trigger_event;

    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (program_id != 0) {
        wash_trigger_event_init(&wash_trigger_event,
            TRIGGER_TYPE_START,
            program_id,
            0,
            "coordinator-start",
            system_context->current_time_ms);
        return wash_cycle_coordinator_submit(system_context, &wash_trigger_event);
    }
    if (wait_timeout_service_should_fire(system_context)) {
        wash_trigger_event_init(&wash_trigger_event,
            TRIGGER_TYPE_TIMEOUT,
            0,
            system_context->wash_session.progress_stage_id,
            "coordinator-timeout",
            system_context->current_time_ms);
        return wash_cycle_coordinator_submit(system_context, &wash_trigger_event);
    }
    return operation_result_ok();
}
