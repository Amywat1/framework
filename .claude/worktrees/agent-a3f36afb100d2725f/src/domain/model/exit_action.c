#include "domain/model/exit_action.h"

#include <string.h>

void segment_exit_actions_init(segment_exit_actions_t *segment_exit_actions)
{
    if (segment_exit_actions == 0)
    {
        return;
    }

    memset(segment_exit_actions, 0, sizeof(*segment_exit_actions));
}

bool segment_exit_actions_has_any(const segment_exit_actions_t *segment_exit_actions)
{
    if (segment_exit_actions == 0)
    {
        return false;
    }
    return segment_exit_actions->stop_roof_brush || segment_exit_actions->stop_side_brush ||
           segment_exit_actions->stop_chemical || segment_exit_actions->close_ro_water ||
           segment_exit_actions->close_dryer || segment_exit_actions->roof_brush_home;
}
