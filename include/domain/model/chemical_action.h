#ifndef DOMAIN_MODEL_CHEMICAL_ACTION_H
#define DOMAIN_MODEL_CHEMICAL_ACTION_H

#include "domain/model/domain_enums.h"

typedef struct chemical_action_t
{
    char channel_id[32];
    chemical_start_condition_t start_condition;
    int duration_ms;
    int retry_limit;
} chemical_action_t;

void chemical_action_init(chemical_action_t *chemical_action, const char *channel_id, int duration_ms);

#endif
