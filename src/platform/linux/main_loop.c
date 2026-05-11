#include "platform/linux/main_loop.h"

#include "application/use_cases/process_wash_trigger.h"
#include "domain/services/trigger_priority_service.h"
#include "domain/services/wait_timeout_service.h"
#include "src/application/coordinators/system_context_private.h"
#include "shared/timeouts.h"

static bool has_pending_timeout_trigger(const system_context_t system_context)
{
    unsigned int index;

    for (index = 0; index < system_context_private_runtime(system_context)->pending_trigger_count; ++index) {
        if (system_context_private_runtime(system_context)->pending_triggers[index].trigger_type == TRIGGER_TYPE_TIMEOUT) {
            return true;
        }
    }
    return false;
}

static void enqueue_timeout_if_needed(system_context_t system_context)
{
    wash_trigger_event_t wash_trigger_event;

    if (!wait_timeout_service_should_fire(&system_context_private_runtime(system_context)->wait_condition, system_context_private_runtime(system_context)->current_time_ms)) {
        return;
    }
    if (system_context_private_runtime(system_context)->pending_trigger_count >= MAX_PENDING_TRIGGER_COUNT || has_pending_timeout_trigger(system_context)) {
        return;
    }
    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_TIMEOUT,
        0,
        system_context_private_runtime(system_context)->wait_condition.reason_code,
        "main-loop-timeout",
        system_context_private_runtime(system_context)->current_time_ms);
    system_context_private_runtime(system_context)->pending_triggers[system_context_private_runtime(system_context)->pending_trigger_count++] = wash_trigger_event;
}

operation_result_t main_loop_submit_trigger(system_context_t system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    result = system_context_private_require_active(system_context);
    if (!result.ok) {
        return result;
    }
    if (wash_trigger_event == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (system_context_private_runtime(system_context)->pending_trigger_count >= MAX_PENDING_TRIGGER_COUNT) {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    system_context_private_runtime(system_context)->pending_triggers[system_context_private_runtime(system_context)->pending_trigger_count++] = *wash_trigger_event;
    return operation_result_ok();
}

static void remove_pending_trigger_at(system_context_t system_context, unsigned int remove_index)
{
    unsigned int index;

    if (remove_index >= system_context_private_runtime(system_context)->pending_trigger_count) {
        return;
    }
    for (index = remove_index + 1; index < system_context_private_runtime(system_context)->pending_trigger_count; ++index) {
        system_context_private_runtime(system_context)->pending_triggers[index - 1] = system_context_private_runtime(system_context)->pending_triggers[index];
    }
    system_context_private_runtime(system_context)->pending_trigger_count -= 1;
}

void main_loop_advance_time(system_context_t system_context, unsigned long elapsed_ms)
{
    if (!system_context_private_require_active(system_context).ok) {
        return;
    }
    system_context_private_runtime(system_context)->current_time_ms += elapsed_ms;
}

operation_result_t main_loop_run(system_context_t system_context)
{
    int best_index;
    unsigned int index;
    operation_result_t result;

    result = system_context_private_require_active(system_context);
    if (!result.ok) {
        return result;
    }

    enqueue_timeout_if_needed(system_context);
    if (system_context_private_runtime(system_context)->pending_trigger_count > 0) {
        best_index = 0;
        for (index = 1; index < system_context_private_runtime(system_context)->pending_trigger_count; ++index) {
            if (trigger_priority_service_compare(&system_context_private_runtime(system_context)->pending_triggers[index],
                &system_context_private_runtime(system_context)->pending_triggers[best_index]) > 0) {
                best_index = (int)index;
            }
        }

        {
            wash_trigger_event_t selected_event = system_context_private_runtime(system_context)->pending_triggers[best_index];
            remove_pending_trigger_at(system_context, (unsigned int)best_index);
            result = process_wash_trigger_execute(system_context, &selected_event);
            if (!result.ok) {
                return result;
            }
        }
    }

    return process_wash_runtime_tick(system_context);
}
