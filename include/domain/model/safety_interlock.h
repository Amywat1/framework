#ifndef DOMAIN_MODEL_SAFETY_INTERLOCK_H
#define DOMAIN_MODEL_SAFETY_INTERLOCK_H

#include "domain/model/domain_enums.h"

typedef struct safety_interlock_t
{
    char interlock_id[32];
    char interlock_name[32];
    interlock_state_t current_state;
    trip_action_t trip_action;
    reset_rule_t reset_rule;
} safety_interlock_t;

void safety_interlock_init(safety_interlock_t *safety_interlock, const char *interlock_id, const char *interlock_name);

#endif
