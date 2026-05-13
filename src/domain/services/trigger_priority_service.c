#include "domain/services/trigger_priority_service.h"

static int trigger_priority(trigger_type_t trigger_type)
{
    switch (trigger_type) {
        case TRIGGER_TYPE_FAULT:
            return 4;
        case TRIGGER_TYPE_STOP:
            return 3;
        case TRIGGER_TYPE_TIMEOUT:
            return 2;
        case TRIGGER_TYPE_HOMING:
            return 1;
        default:
            return 0;
    }
}

int trigger_priority_service_compare(const wash_trigger_event_t *left_event, const wash_trigger_event_t *right_event)
{
    if (left_event == 0 && right_event == 0) {
        return 0;
    }
    if (left_event == 0) {
        return -1;
    }
    if (right_event == 0) {
        return 1;
    }
    return trigger_priority(left_event->trigger_type) - trigger_priority(right_event->trigger_type);
}
