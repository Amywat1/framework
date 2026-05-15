#include "application/coordinators/runtime_result_projection.h"

#include <string.h>

static void write_projection_field(char *target, size_t target_size, const char *value)
{
    if (target == 0 || target_size == 0)
    {
        return;
    }

    if (value != 0 && value[0] != '\0')
    {
        strncpy(target, value, target_size - 1);
        target[target_size - 1] = '\0';
        return;
    }

    strncpy(target, "none", target_size - 1);
    target[target_size - 1] = '\0';
}

void runtime_result_projection_init(runtime_result_projection_t *runtime_result_projection)
{
    if (runtime_result_projection == 0)
    {
        return;
    }

    memset(runtime_result_projection, 0, sizeof(*runtime_result_projection));
    runtime_result_projection->log_kind = RUNTIME_EVENT_LOG_NONE;
}

void runtime_result_projection_set_latest_result(runtime_result_projection_t *runtime_result_projection,
                                                 const char *result_code, const char *reason_code)
{
    if (runtime_result_projection == 0)
    {
        return;
    }

    runtime_result_projection->updates_latest_result = true;
    write_projection_field(runtime_result_projection->latest_result_code,
                           sizeof(runtime_result_projection->latest_result_code), result_code);
    write_projection_field(runtime_result_projection->latest_reason_code,
                           sizeof(runtime_result_projection->latest_reason_code), reason_code);
}

void runtime_result_projection_set_transition(runtime_result_projection_t *runtime_result_projection,
                                              transition_entity_type_t transition_entity, const char *entity_id,
                                              trigger_type_t trigger_type, const char *previous_state,
                                              const char *current_state, const char *result_code,
                                              const char *reason_code, runtime_event_log_kind_t runtime_event_log_kind)
{
    if (runtime_result_projection == 0)
    {
        return;
    }

    runtime_result_projection->records_transition = true;
    runtime_result_projection->transition_entity = transition_entity;
    runtime_result_projection->trigger_type = trigger_type;
    runtime_result_projection->log_kind = runtime_event_log_kind;
    write_projection_field(runtime_result_projection->entity_id, sizeof(runtime_result_projection->entity_id),
                           entity_id);
    write_projection_field(runtime_result_projection->previous_state, sizeof(runtime_result_projection->previous_state),
                           previous_state);
    write_projection_field(runtime_result_projection->current_state, sizeof(runtime_result_projection->current_state),
                           current_state);
    write_projection_field(runtime_result_projection->transition_result_code,
                           sizeof(runtime_result_projection->transition_result_code), result_code);
    write_projection_field(runtime_result_projection->transition_reason_code,
                           sizeof(runtime_result_projection->transition_reason_code), reason_code);
}
