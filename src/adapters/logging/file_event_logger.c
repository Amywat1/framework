#include "adapters/logging/file_event_logger.h"

#include <stdio.h>
#include <string.h>

typedef struct file_logger_context_t {
    char log_path[260];
} file_logger_context_t;

static file_logger_context_t g_logger_context;

static int log_message_impl(void *context, event_type_t event_type, const char *message)
{
    FILE *file_handle;
    file_logger_context_t *logger_context = (file_logger_context_t *)context;

    file_handle = fopen(logger_context->log_path, "a");
    if (file_handle == 0) {
        return -1;
    }
    fprintf(file_handle, "%d,%s\n", (int)event_type, message != 0 ? message : "");
    fclose(file_handle);
    return 0;
}

void file_event_logger_init(system_context_t *system_context, const char *log_path)
{
    memset(&g_logger_context, 0, sizeof(g_logger_context));
    strncpy(g_logger_context.log_path, log_path, sizeof(g_logger_context.log_path) - 1);

    system_context->event_logger_port.context = &g_logger_context;
    system_context->event_logger_port.log_message = log_message_impl;
}

