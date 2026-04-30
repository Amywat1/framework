#ifndef DOMAIN_PORTS_EVENT_LOGGER_PORT_H
#define DOMAIN_PORTS_EVENT_LOGGER_PORT_H

#include "domain/model/domain_enums.h"

typedef struct event_logger_port_t {
    void *context;
    int (*log_message)(void *context, event_type_t event_type, const char *message);
} event_logger_port_t;

#endif

