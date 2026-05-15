#ifndef DOMAIN_MODEL_FAULT_EVENT_H
#define DOMAIN_MODEL_FAULT_EVENT_H

#include "domain/model/domain_enums.h"
#include <stdbool.h>

typedef struct fault_event_t
{
    char event_id[32];
    char message[128];
    fault_severity_t severity;
    fault_class_t fault_class;
    int disposition;
    bool operator_ack_required;
} fault_event_t;

void fault_event_init(fault_event_t *fault_event, const char *event_id, const char *message, fault_class_t fault_class,
                      fault_severity_t severity);

#endif
