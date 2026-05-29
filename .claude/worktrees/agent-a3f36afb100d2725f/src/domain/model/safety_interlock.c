#include "domain/model/safety_interlock.h"

#include <string.h>

void safety_interlock_init(safety_interlock_t *safety_interlock, const char *interlock_id, const char *interlock_name)
{
    if (safety_interlock == 0)
    {
        return;
    }
    memset(safety_interlock, 0, sizeof(*safety_interlock));
    if (interlock_id != 0)
    {
        strncpy(safety_interlock->interlock_id, interlock_id, sizeof(safety_interlock->interlock_id) - 1);
    }
    if (interlock_name != 0)
    {
        strncpy(safety_interlock->interlock_name, interlock_name, sizeof(safety_interlock->interlock_name) - 1);
    }
    safety_interlock->current_state = INTERLOCK_READY;
    safety_interlock->trip_action = TRIP_ACTION_STOP_IMMEDIATELY;
    safety_interlock->reset_rule = RESET_RULE_MANUAL;
}
