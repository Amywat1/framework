#include "domain/model/wash_trigger_event.h"

#include <stdio.h>
#include <string.h>

void wash_trigger_event_init(wash_trigger_event_t *wash_trigger_event,
    trigger_type_t trigger_type,
    const char *program_id,
    const char *signal_code,
    const char *correlation_key,
    unsigned long occurred_at_ms)
{
    if (wash_trigger_event == 0) {
        return;
    }

    memset(wash_trigger_event, 0, sizeof(*wash_trigger_event));
    snprintf(wash_trigger_event->trigger_id,
        sizeof(wash_trigger_event->trigger_id),
        "trigger-%lu",
        occurred_at_ms);
    wash_trigger_event->trigger_type = trigger_type;
    wash_trigger_event->occurred_at_ms = occurred_at_ms;
    if (program_id != 0) {
        strncpy(wash_trigger_event->program_id, program_id, sizeof(wash_trigger_event->program_id) - 1);
    }
    if (signal_code != 0) {
        strncpy(wash_trigger_event->signal_code, signal_code, sizeof(wash_trigger_event->signal_code) - 1);
    }
    if (correlation_key != 0) {
        strncpy(wash_trigger_event->correlation_key, correlation_key, sizeof(wash_trigger_event->correlation_key) - 1);
    }
}
