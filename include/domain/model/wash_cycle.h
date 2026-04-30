#ifndef DOMAIN_MODEL_WASH_CYCLE_H
#define DOMAIN_MODEL_WASH_CYCLE_H

#include "domain/model/domain_enums.h"
#include "domain/model/wash_program.h"

typedef struct wash_cycle_t {
    char cycle_id[32];
    char selected_program_id[32];
    char detected_vehicle_type_id[32];
    cycle_state_t cycle_state;
    int current_stage_index;
    result_code_t result_code;
} wash_cycle_t;

void wash_cycle_init(wash_cycle_t *wash_cycle, const char *cycle_id, const char *selected_program_id);
void wash_cycle_set_state(wash_cycle_t *wash_cycle, cycle_state_t cycle_state);
void wash_cycle_finish(wash_cycle_t *wash_cycle, result_code_t result_code);
const wash_stage_t *wash_cycle_current_stage(const wash_cycle_t *wash_cycle, const wash_program_t *wash_program);

#endif

