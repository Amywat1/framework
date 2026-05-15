#include "domain/model/state_transition_record.h"

#include <stdio.h>
#include <string.h>

void state_transition_record_init(state_transition_record_t *state_transition_record,
                                  transition_entity_type_t entity_type, const char *entity_id,
                                  trigger_type_t trigger_type, const char *previous_state, const char *current_state,
                                  const char *result_code, const char *reason_code, unsigned long recorded_at_ms)
{
    if (state_transition_record == 0)
    {
        return;
    }

    memset(state_transition_record, 0, sizeof(*state_transition_record));
    snprintf(state_transition_record->record_id, sizeof(state_transition_record->record_id), "record-%lu",
             recorded_at_ms);
    state_transition_record->entity_type = entity_type;
    state_transition_record->trigger_type = trigger_type;
    state_transition_record->recorded_at_ms = recorded_at_ms;
    if (entity_id != 0)
    {
        strncpy(state_transition_record->entity_id, entity_id, sizeof(state_transition_record->entity_id) - 1);
    }
    if (previous_state != 0)
    {
        strncpy(state_transition_record->previous_state, previous_state,
                sizeof(state_transition_record->previous_state) - 1);
    }
    if (current_state != 0)
    {
        strncpy(state_transition_record->current_state, current_state,
                sizeof(state_transition_record->current_state) - 1);
    }
    if (result_code != 0)
    {
        strncpy(state_transition_record->result_code, result_code, sizeof(state_transition_record->result_code) - 1);
    }
    if (reason_code != 0)
    {
        strncpy(state_transition_record->reason_code, reason_code, sizeof(state_transition_record->reason_code) - 1);
    }
}
