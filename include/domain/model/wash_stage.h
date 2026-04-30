#ifndef DOMAIN_MODEL_WASH_STAGE_H
#define DOMAIN_MODEL_WASH_STAGE_H

#include <stdbool.h>
#include "domain/model/chemical_action.h"
#include "domain/model/domain_enums.h"

#define MAX_STAGE_ACTIONS (4)

typedef struct wash_stage_t {
    char stage_id[32];
    char stage_name[32];
    int sequence_no;
    gantry_motion_mode_t gantry_motion_mode;
    int traverse_count;
    brush_mode_t roof_brush_mode;
    brush_mode_t side_brush_mode;
    bool rinse_enabled;
    int stage_timeout_ms;
    bool skip_on_resource_fault;
    chemical_action_t chemical_actions[MAX_STAGE_ACTIONS];
    int chemical_action_count;
} wash_stage_t;

void wash_stage_init(wash_stage_t *wash_stage, const char *stage_id, const char *stage_name, int sequence_no);
int wash_stage_add_chemical_action(wash_stage_t *wash_stage, const chemical_action_t *chemical_action);

#endif

