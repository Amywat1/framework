#include "application/coordinators/runtime_event_recorder.h"

#include <string.h>

#include "domain/model/state_transition_record.h"
#include "src/application/coordinators/device_runtime_private.h"

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

void runtime_event_recorder_set_latest_result(device_runtime_t system_context, const char *result_code,
                                              const char *reason_code)
{
    if (!device_runtime_private_require_active(system_context).ok)
    {
        return;
    }
    device_runtime_private_set_latest_result(system_context, result_code, reason_code);
}

void runtime_event_recorder_apply_projection(device_runtime_t system_context,
                                             const runtime_result_projection_t *runtime_result_projection)
{
    const event_logger_port_t *event_logger_port;
    state_transition_record_t *last_transition_record;

    if (!device_runtime_private_require_active(system_context).ok)
    {
        return;
    }
    if (runtime_result_projection == 0)
    {
        return;
    }

    if (runtime_result_projection->updates_latest_result)
    {
        runtime_event_recorder_set_latest_result(system_context, runtime_result_projection->latest_result_code,
                                                 runtime_result_projection->latest_reason_code);
    }
    if (!runtime_result_projection->records_transition)
    {
        return;
    }

    last_transition_record = device_runtime_private_last_transition_record_mutable(system_context);
    event_logger_port = device_runtime_private_event_logger_port(system_context);
    if (last_transition_record == 0 || event_logger_port == 0)
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
                                 device_runtime_current_time_ms(system_context));

    switch (runtime_result_projection->log_kind)
    {
    case RUNTIME_EVENT_LOG_TRANSITION:
        if (event_logger_port->log_transition != 0)
        {
            event_logger_port->log_transition(event_logger_port->context, last_transition_record);
        }
        break;
    case RUNTIME_EVENT_LOG_REJECTION:
        if (event_logger_port->log_rejection != 0)
        {
            event_logger_port->log_rejection(event_logger_port->context, last_transition_record);
        }
        break;
    case RUNTIME_EVENT_LOG_IGNORED:
        if (event_logger_port->log_ignored != 0)
        {
            event_logger_port->log_ignored(event_logger_port->context, last_transition_record);
        }
        break;
    case RUNTIME_EVENT_LOG_NONE:
    default:
        break;
    }
}
