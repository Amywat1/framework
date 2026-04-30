#include "domain/model/chemical_channel.h"

#include <string.h>

void chemical_channel_init(chemical_channel_t *chemical_channel, const char *channel_id, const char *channel_name)
{
    if (chemical_channel == 0) {
        return;
    }

    memset(chemical_channel, 0, sizeof(*chemical_channel));
    if (channel_id != 0) {
        strncpy(chemical_channel->channel_id, channel_id, sizeof(chemical_channel->channel_id) - 1);
    }
    if (channel_name != 0) {
        strncpy(chemical_channel->channel_name, channel_name, sizeof(chemical_channel->channel_name) - 1);
    }
    chemical_channel->enabled = true;
    chemical_channel->resource_state = RESOURCE_STATE_NORMAL;
    chemical_channel->default_dose_ms = 1000;
    chemical_channel->fault_policy = FAULT_POLICY_DEGRADE;
}

