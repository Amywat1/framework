#include "application/coordinators/compatibility_trigger_runner.h"

#include <stdio.h>
#include <string.h>

#include "platform/linux/main_loop.h"
#include "shared/error_codes.h"

static bool has_pending_trigger_id(const system_context_t *system_context, const char *trigger_id)
{
    unsigned int index;

    if (system_context == 0 || trigger_id == 0 || trigger_id[0] == '\0') {
        return false;
    }
    for (index = 0; index < system_context->pending_trigger_count; ++index) {
        if (strcmp(system_context->pending_triggers[index].trigger_id, trigger_id) == 0) {
            return true;
        }
    }
    return false;
}

static void assign_compatibility_trigger_id(system_context_t *system_context, wash_trigger_event_t *wash_trigger_event)
{
    if (system_context == 0 || wash_trigger_event == 0 || wash_trigger_event->trigger_id[0] != '\0') {
        return;
    }

    snprintf(wash_trigger_event->trigger_id,
        sizeof(wash_trigger_event->trigger_id),
        "compat-%lu-%u",
        system_context->current_time_ms,
        system_context->pending_trigger_count);
    strncpy(wash_trigger_event->source, "compat-use-case", sizeof(wash_trigger_event->source) - 1);
    wash_trigger_event->source[sizeof(wash_trigger_event->source) - 1] = '\0';
}

operation_result_t compatibility_trigger_runner_execute(system_context_t *system_context,
    const wash_trigger_event_t *wash_trigger_event)
{
    wash_trigger_event_t queued_trigger_event;
    operation_result_t result;

    if (system_context == 0 || wash_trigger_event == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    queued_trigger_event = *wash_trigger_event;
    assign_compatibility_trigger_id(system_context, &queued_trigger_event);

    result = main_loop_submit_trigger(system_context, &queued_trigger_event);
    if (!result.ok) {
        return result;
    }

    while (has_pending_trigger_id(system_context, queued_trigger_event.trigger_id)) {
        result = main_loop_run(system_context);
        if (!result.ok && has_pending_trigger_id(system_context, queued_trigger_event.trigger_id)) {
            return result;
        }
    }

    return result;
}
