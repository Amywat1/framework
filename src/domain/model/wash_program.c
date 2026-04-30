#include "domain/model/wash_program.h"

#include <string.h>

void wash_program_init(wash_program_t *wash_program, const char *program_id, const char *program_name)
{
    if (wash_program == 0) {
        return;
    }

    memset(wash_program, 0, sizeof(*wash_program));
    if (program_id != 0) {
        strncpy(wash_program->program_id, program_id, sizeof(wash_program->program_id) - 1);
    }
    if (program_name != 0) {
        strncpy(wash_program->program_name, program_name, sizeof(wash_program->program_name) - 1);
    }
    wash_program->enabled = true;
    wash_program->default_timeout_ms = 5000;
    wash_program->revision = 1;
}

int wash_program_add_stage(wash_program_t *wash_program, const wash_stage_t *wash_stage)
{
    if (wash_program == 0 || wash_stage == 0) {
        return -1;
    }
    if (wash_program->stage_count >= MAX_PROGRAM_STAGES) {
        return -1;
    }
    wash_program->stages[wash_program->stage_count++] = *wash_stage;
    return 0;
}

const wash_stage_t *wash_program_get_stage(const wash_program_t *wash_program, int index)
{
    if (wash_program == 0 || index < 0 || index >= wash_program->stage_count) {
        return 0;
    }
    return &wash_program->stages[index];
}

