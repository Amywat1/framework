#ifndef DOMAIN_MODEL_CHEMICAL_CHANNEL_H
#define DOMAIN_MODEL_CHEMICAL_CHANNEL_H

#include "domain/model/domain_enums.h"
#include <stdbool.h>

typedef struct chemical_channel_t
{
    char channel_id[32];
    char channel_name[32];
    bool enabled;
    resource_state_t resource_state;
    int low_level_threshold;
    int default_dose_ms;
    fault_policy_t fault_policy;
} chemical_channel_t;

void chemical_channel_init(chemical_channel_t *chemical_channel, const char *channel_id, const char *channel_name);

#endif
