#include "domain/model/chemical_action.h"

#include <string.h>

void chemical_action_init(chemical_action_t *chemical_action, const char *channel_id, int duration_ms)
{
    if (chemical_action == 0)
    {
        return;
    }

    memset(chemical_action, 0, sizeof(*chemical_action));
    if (channel_id != 0)
    {
        strncpy(chemical_action->channel_id, channel_id, sizeof(chemical_action->channel_id) - 1);
    }
    chemical_action->start_condition = CHEMICAL_START_ON_STAGE_START;
    chemical_action->duration_ms = duration_ms;
    chemical_action->retry_limit = 1;
}
