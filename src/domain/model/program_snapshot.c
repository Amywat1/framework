#include "domain/model/program_snapshot.h"

#include <stdio.h>
#include <string.h>

void program_snapshot_reset(program_snapshot_t *program_snapshot)
{
    if (program_snapshot == 0) {
        return;
    }
    memset(program_snapshot, 0, sizeof(*program_snapshot));
    program_snapshot->validation_result = PROGRAM_SNAPSHOT_VALIDATION_UNAVAILABLE;
}

void program_snapshot_capture(program_snapshot_t *program_snapshot,
    const char *program_id,
    int source_revision,
    unsigned long captured_at_ms,
    const wash_program_t *wash_program,
    program_snapshot_validation_t validation_result)
{
    if (program_snapshot == 0) {
        return;
    }

    program_snapshot_reset(program_snapshot);
    if (program_id != 0) {
        strncpy(program_snapshot->source_program_id, program_id, sizeof(program_snapshot->source_program_id) - 1);
    }
    snprintf(program_snapshot->program_snapshot_id,
        sizeof(program_snapshot->program_snapshot_id),
        "snapshot-%lu",
        captured_at_ms);
    program_snapshot->source_revision = source_revision;
    program_snapshot->captured_at_ms = captured_at_ms;
    program_snapshot->validation_result = validation_result;
    snprintf(program_snapshot->snapshot_hash,
        sizeof(program_snapshot->snapshot_hash),
        "%s-%d-%lu",
        program_snapshot->source_program_id,
        source_revision,
        captured_at_ms);
    snprintf(program_snapshot->snapshot_payload_ref,
        sizeof(program_snapshot->snapshot_payload_ref),
        "program:%s@%d",
        program_snapshot->source_program_id,
        source_revision);
    if (wash_program != 0) {
        program_snapshot->frozen_program = *wash_program;
    }
}
