#ifndef DOMAIN_EVENTS_EVENT_TYPES_H
#define DOMAIN_EVENTS_EVENT_TYPES_H

#include "domain/model/domain_enums.h"

typedef struct {
    event_type_t event_type;
    cycle_state_t cycle_state;
    result_code_t result_code;
    fault_class_t fault_class;
} domain_event_t;

#endif

