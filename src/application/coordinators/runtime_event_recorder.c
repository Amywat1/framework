#include "application/coordinators/runtime_event_recorder.h"

#include <string.h>

#include "domain/model/state_transition_record.h"
#include "src/application/coordinators/control_context_private.h"

/*
 * runtime_result_projection_* 的实现与 recorder 放在同一文件中，
 * 让投影结构的填充和最终记录落点保持在同一个维护位置。
 */
static const char *safe_runtime_field(const char *value) { return (value != 0 && value[0] != '\0') ? value : "none"; }

static void write_projection_field(char *target, size_t target_size, const char *value)
{
    if (target == 0 || target_size == 0)
    {
        return;
    }

    strncpy(target, safe_runtime_field(value), target_size - 1);
    target[target_size - 1] = '\0';
}

void runtime_result_projection_init(runtime_result_projection_t *runtime_result_projection)
{
    if (runtime_result_projection == 0)
    {
        return;
    }

    memset(runtime_result_projection, 0, sizeof(*runtime_result_projection));
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
                                              const char *reason_code)
{
    if (runtime_result_projection == 0)
    {
        return;
    }

    runtime_result_projection->records_transition = true;
    runtime_result_projection->transition_entity = transition_entity;
    runtime_result_projection->trigger_type = trigger_type;
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

void runtime_event_recorder_set_latest_result(const char *result_code, const char *reason_code)
{
    if (!control_context_require_active().ok)
    {
        return;
    }
    control_context_private_set_latest_result(result_code, reason_code);
}

void runtime_event_recorder_apply_projection(const runtime_result_projection_t *runtime_result_projection)
{
    state_transition_record_t *last_transition_record;

    if (!control_context_require_active().ok)
    {
        return;
    }
    if (runtime_result_projection == 0)
    {
        return;
    }

    if (runtime_result_projection->updates_latest_result)
    {
        runtime_event_recorder_set_latest_result(runtime_result_projection->latest_result_code,
                                                 runtime_result_projection->latest_reason_code);
    }
    if (!runtime_result_projection->records_transition)
    {
        return;
    }

    last_transition_record = control_context_private_last_transition_record_mutable();
    if (last_transition_record == 0)
    {
        return;
    }

    state_transition_record_init(last_transition_record, runtime_result_projection->transition_entity,
                                 safe_runtime_field(runtime_result_projection->entity_id),
                                 runtime_result_projection->trigger_type,
                                 safe_runtime_field(runtime_result_projection->previous_state),
                                 safe_runtime_field(runtime_result_projection->current_state),
                                 safe_runtime_field(runtime_result_projection->transition_result_code),
                                 safe_runtime_field(runtime_result_projection->transition_reason_code),
                                 control_context_current_time_ms());
}
