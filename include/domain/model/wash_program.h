#ifndef DOMAIN_MODEL_WASH_PROGRAM_H
#define DOMAIN_MODEL_WASH_PROGRAM_H

#include <stdbool.h>

#include "domain/model/wash_segment.h"

#define MAX_PROGRAM_SEGMENTS (8)

/**
 * @file wash_program.h
 * @brief 定义工步程序模型。
 */
typedef struct wash_program_t {
    char program_id[32];
    char program_name[32];
    bool enabled;
    int default_segment_timeout_ms;
    int default_exit_timeout_ms;
    int revision;
    wash_segment_t segments[MAX_PROGRAM_SEGMENTS];
    int segment_count;
} wash_program_t;

typedef wash_program_t wash_program_v1_t;

void wash_program_init(wash_program_t *wash_program, const char *program_id, const char *program_name);
int wash_program_add_segment(wash_program_t *wash_program, const wash_segment_t *wash_segment);
const wash_segment_t *wash_program_get_segment(const wash_program_t *wash_program, int index);

#endif
