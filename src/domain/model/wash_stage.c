#include "domain/model/wash_stage.h"

#include <string.h>

void wash_stage_init(wash_stage_t *wash_stage, const char *stage_id, const char *stage_name, int sequence_no)
{
    if (wash_stage == 0) {
        return;
    }

    memset(wash_stage, 0, sizeof(*wash_stage));
    if (stage_id != 0) {
        strncpy(wash_stage->stage_id, stage_id, sizeof(wash_stage->stage_id) - 1);
    }
    if (stage_name != 0) {
        strncpy(wash_stage->stage_name, stage_name, sizeof(wash_stage->stage_name) - 1);
    }
    wash_stage->sequence_no = sequence_no;
    wash_stage->gantry_motion_mode = GANTRY_MOTION_STOP;
    wash_stage->roof_brush_mode = BRUSH_MODE_DISABLED;
    wash_stage->side_brush_mode = BRUSH_MODE_DISABLED;
    wash_stage->stage_timeout_ms = 1000;
}

int wash_stage_add_chemical_action(wash_stage_t *wash_stage, const chemical_action_t *chemical_action)
{
    if (wash_stage == 0 || chemical_action == 0) {
        return -1;
    }
    if (wash_stage->chemical_action_count >= MAX_STAGE_ACTIONS) {
        return -1;
    }
    wash_stage->chemical_actions[wash_stage->chemical_action_count++] = *chemical_action;
    return 0;
}

