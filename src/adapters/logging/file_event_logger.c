#include "adapters/logging/file_event_logger.h"

#include <stdio.h>
#include <string.h>

#include "application/coordinators/system_context.h"
#include "domain/model/state_transition_record.h"
#include "src/application/coordinators/system_context_private.h"

typedef struct file_logger_context_t {
    char log_path[260];
} file_logger_context_t;

static file_logger_context_t g_logger_contexts[SYSTEM_CONTEXT_POOL_CAPACITY];

static int log_message_impl(void *context, trigger_type_t trigger_type, const char *message)
{
    FILE *file_handle;
    file_logger_context_t *logger_context = (file_logger_context_t *)context;

    file_handle = fopen(logger_context->log_path, "a");
    if (file_handle == 0) {
        return -1;
    }
    fprintf(file_handle, "%d,%s\n", (int)trigger_type, message != 0 ? message : "");
    fclose(file_handle);
    return 0;
}

static int write_transition_record(void *context, const char *prefix, const state_transition_record_t *state_transition_record)
{
    FILE *file_handle;
    file_logger_context_t *logger_context = (file_logger_context_t *)context;

    if (state_transition_record == 0) {
        return -1;
    }
    file_handle = fopen(logger_context->log_path, "a");
    if (file_handle == 0) {
        return -1;
    }
    fprintf(file_handle,
        "%s,%d,%s,%d,%s,%s,%s,%s,%lu\n",
        prefix,
        (int)state_transition_record->entity_type,
        state_transition_record->entity_id,
        (int)state_transition_record->trigger_type,
        state_transition_record->previous_state,
        state_transition_record->current_state,
        state_transition_record->result_code,
        state_transition_record->reason_code,
        state_transition_record->recorded_at_ms);
    fclose(file_handle);
    return 0;
}

static int log_transition_impl(void *context, const state_transition_record_t *state_transition_record)
{
    return write_transition_record(context, "transition", state_transition_record);
}

static int log_rejection_impl(void *context, const state_transition_record_t *state_transition_record)
{
    return write_transition_record(context, "rejection", state_transition_record);
}

static int log_ignored_impl(void *context, const state_transition_record_t *state_transition_record)
{
    return write_transition_record(context, "ignored", state_transition_record);
}

void file_event_logger_init(system_context_t system_context, const char *log_path)
{
    event_logger_port_t event_logger_port;
    file_logger_context_t *logger_context;
    unsigned int slot_index;

    if (log_path == 0) {
        return;
    }
    slot_index = system_context_private_slot_index(system_context);
    if (slot_index == SYSTEM_CONTEXT_POOL_INVALID_INDEX) {
        return;
    }
    logger_context = &g_logger_contexts[slot_index];
    memset(logger_context, 0, sizeof(*logger_context));
    strncpy(logger_context->log_path, log_path, sizeof(logger_context->log_path) - 1);
    memset(&event_logger_port, 0, sizeof(event_logger_port));
    event_logger_port.context = logger_context;
    event_logger_port.log_message = log_message_impl;
    event_logger_port.log_transition = log_transition_impl;
    event_logger_port.log_rejection = log_rejection_impl;
    event_logger_port.log_ignored = log_ignored_impl;
    system_context_set_event_logger_port(system_context, &event_logger_port);
}
