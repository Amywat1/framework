#ifndef ADAPTERS_LOGGING_FILE_EVENT_LOGGER_H
#define ADAPTERS_LOGGING_FILE_EVENT_LOGGER_H

#include "application/coordinators/system_context.h"

void file_event_logger_init(system_context_t *system_context, const char *log_path);

#endif

