#ifndef DOMAIN_PORTS_EVENT_LOGGER_PORT_H
#define DOMAIN_PORTS_EVENT_LOGGER_PORT_H

#include "domain/model/domain_enums.h"

struct state_transition_record_t;

typedef struct event_logger_port_t {
    void *context;
    int (*log_message)(void *context, event_type_t event_type, const char *message);
    int (*log_transition)(void *context, const struct state_transition_record_t *state_transition_record);
    int (*log_rejection)(void *context, const struct state_transition_record_t *state_transition_record);
    int (*log_ignored)(void *context, const struct state_transition_record_t *state_transition_record);
} event_logger_port_t;

#endif
