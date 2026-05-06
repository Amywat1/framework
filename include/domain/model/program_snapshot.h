#ifndef DOMAIN_MODEL_PROGRAM_SNAPSHOT_H
#define DOMAIN_MODEL_PROGRAM_SNAPSHOT_H

#include "domain/model/domain_enums.h"
#include "domain/model/wash_program.h"

/**
 * @file program_snapshot.h
 * @brief 定义会话绑定的程序快照。
 */
typedef struct program_snapshot_t {
    char program_snapshot_id[32];
    char source_program_id[32];
    int source_revision;
    unsigned long captured_at_ms;
    program_snapshot_validation_t validation_result;
    char snapshot_hash[64];
    char snapshot_payload_ref[64];
    wash_program_t frozen_program;
} program_snapshot_t;

void program_snapshot_reset(program_snapshot_t *program_snapshot);
void program_snapshot_capture(program_snapshot_t *program_snapshot,
    const char *program_id,
    int source_revision,
    unsigned long captured_at_ms,
    const wash_program_t *wash_program,
    program_snapshot_validation_t validation_result);

#endif
