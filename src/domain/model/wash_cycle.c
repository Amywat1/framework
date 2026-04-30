#include "domain/model/wash_cycle.h"

#include <string.h>

void wash_cycle_init(wash_cycle_t *wash_cycle, const char *cycle_id, const char *selected_program_id)
{
    if (wash_cycle == 0) {
        return;
    }
    memset(wash_cycle, 0, sizeof(*wash_cycle));
    if (cycle_id != 0) {
        strncpy(wash_cycle->cycle_id, cycle_id, sizeof(wash_cycle->cycle_id) - 1);
    }
    if (selected_program_id != 0) {
        strncpy(wash_cycle->selected_program_id, selected_program_id, sizeof(wash_cycle->selected_program_id) - 1);
    }
    wash_cycle->cycle_state = CYCLE_STATE_IDLE;
    wash_cycle->result_code = RESULT_CODE_START_FAILED;
}

void wash_cycle_set_state(wash_cycle_t *wash_cycle, cycle_state_t cycle_state)
{
    if (wash_cycle == 0) {
        return;
    }
    wash_cycle->cycle_state = cycle_state;
}

void wash_cycle_finish(wash_cycle_t *wash_cycle, result_code_t result_code)
{
    if (wash_cycle == 0) {
        return;
    }
    wash_cycle->cycle_state = CYCLE_STATE_COMPLETED;
    wash_cycle->result_code = result_code;
}

const wash_stage_t *wash_cycle_current_stage(const wash_cycle_t *wash_cycle, const wash_program_t *wash_program)
{
    if (wash_cycle == 0 || wash_program == 0) {
        return 0;
    }
    return wash_program_get_stage(wash_program, wash_cycle->current_stage_index);
}

