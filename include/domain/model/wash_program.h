#ifndef DOMAIN_MODEL_WASH_PROGRAM_H
#define DOMAIN_MODEL_WASH_PROGRAM_H

#include <stdbool.h>
#include "domain/model/wash_stage.h"

#define MAX_PROGRAM_STAGES (8)

typedef struct wash_program_t {
    char program_id[32];
    char program_name[32];
    bool enabled;
    int default_timeout_ms;
    int revision;
    wash_stage_t stages[MAX_PROGRAM_STAGES];
    int stage_count;
} wash_program_t;

void wash_program_init(wash_program_t *wash_program, const char *program_id, const char *program_name);
int wash_program_add_stage(wash_program_t *wash_program, const wash_stage_t *wash_stage);
const wash_stage_t *wash_program_get_stage(const wash_program_t *wash_program, int index);

#endif

