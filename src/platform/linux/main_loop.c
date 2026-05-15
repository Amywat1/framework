#include "platform/linux/main_loop.h"

#include "application/coordinators/system_context.h"
#include "application/use_cases/process_wash_trigger.h"
#include "domain/services/trigger_priority_service.h"
#include "domain/services/wait_timeout_service.h"
#include "shared/error_codes.h"
#include "shared/timeouts.h"

static bool has_pending_timeout_trigger(const system_context_t system_context)
{
    const wash_trigger_event_t *wash_trigger_event;
    unsigned int index;

    for (index = 0; index < system_context_pending_trigger_count(system_context); ++index) {
        wash_trigger_event = system_context_pending_trigger_at(system_context, index);
        if (wash_trigger_event != 0 && wash_trigger_event->trigger_type == TRIGGER_TYPE_TIMEOUT) {
            return true;
        }
    }
    return false;
}

static void enqueue_timeout_if_needed(system_context_t system_context)
{
    const wait_condition_t *wait_condition;
    wash_trigger_event_t wash_trigger_event;
    unsigned long current_time_ms;

    wait_condition = system_context_wait_condition(system_context);
    current_time_ms = system_context_current_time_ms(system_context);
    if (wait_condition == 0 || !wait_timeout_service_should_fire(wait_condition, current_time_ms)) {
        return;
    }
    if (system_context_pending_trigger_count(system_context) >= MAX_PENDING_TRIGGER_COUNT || has_pending_timeout_trigger(system_context)) {
        return;
    }
    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_TIMEOUT,
        0,
        wait_condition->reason_code,
        "main-loop-timeout",
        current_time_ms);
    (void)system_context_append_trigger(system_context, &wash_trigger_event);
}

operation_result_t main_loop_submit_trigger(system_context_t system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    result = system_context_require_active(system_context);
    if (!result.ok) {
        return result;
    }
    if (wash_trigger_event == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    return system_context_append_trigger(system_context, wash_trigger_event);
}

void main_loop_advance_time(system_context_t system_context, unsigned long elapsed_ms)
{
    if (!system_context_require_active(system_context).ok) {
        return;
    }
    system_context_advance_time(system_context, elapsed_ms);
}

operation_result_t main_loop_run(system_context_t system_context)
{
    int best_index;
    unsigned int index;
    const wash_trigger_event_t *selected_event_ref;
    wash_trigger_event_t selected_event;
    const wash_trigger_event_t *candidate;
    const wash_trigger_event_t *best_candidate;
    operation_result_t result;

    result = system_context_require_active(system_context);
    if (!result.ok) {
        return result;
    }

    enqueue_timeout_if_needed(system_context);
    if (system_context_pending_trigger_count(system_context) > 0u) {
        best_index = 0;
        for (index = 1; index < system_context_pending_trigger_count(system_context); ++index) {
            candidate = system_context_pending_trigger_at(system_context, index);
            best_candidate = system_context_pending_trigger_at(system_context, (unsigned int)best_index);
            if (candidate != 0
                && best_candidate != 0
                && trigger_priority_service_compare(candidate, best_candidate) > 0) {
                best_index = (int)index;
            }
        }

        selected_event_ref = system_context_pending_trigger_at(system_context, (unsigned int)best_index);
        if (selected_event_ref == 0) {
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }
        selected_event = *selected_event_ref;
        system_context_remove_pending_trigger_at(system_context, (unsigned int)best_index);
        result = process_wash_trigger_execute(system_context, &selected_event);
        if (!result.ok) {
            return result;
        }
    }

    return process_wash_runtime_tick(system_context);
}
