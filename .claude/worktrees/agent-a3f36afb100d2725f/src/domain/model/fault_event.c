#include "domain/model/fault_event.h"

#include <string.h>

void fault_event_init(fault_event_t *fault_event, const char *event_id, const char *message, fault_class_t fault_class,
                      fault_severity_t severity)
{
    if (fault_event == 0)
    {
        return;
    }
    memset(fault_event, 0, sizeof(*fault_event));
    if (event_id != 0)
    {
        strncpy(fault_event->event_id, event_id, sizeof(fault_event->event_id) - 1);
    }
    if (message != 0)
    {
        strncpy(fault_event->message, message, sizeof(fault_event->message) - 1);
    }
    fault_event->fault_class = fault_class;
    fault_event->severity = severity;
    fault_event->operator_ack_required = (severity == FAULT_SEVERITY_SAFETY);
}
