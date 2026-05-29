#include "domain/model/program_snapshot.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief 重置程序快照对象。
 * @param program_snapshot 快照对象。
 */
void program_snapshot_reset(program_snapshot_t *program_snapshot)
{
    if (program_snapshot == 0)
    {
        return;
    }
    memset(program_snapshot, 0, sizeof(*program_snapshot));
    program_snapshot->validation_result = PROGRAM_SNAPSHOT_VALIDATION_UNAVAILABLE;
}

/**
 * @brief 根据当前程序内容抓取并写入程序快照。
 * @param program_snapshot 快照对象。
 * @param program_id 源程序 ID，可为空。
 * @param source_revision 源程序版本号。
 * @param captured_at_ms 抓取时间。
 * @param wash_program 源程序对象，可为空。
 * @param validation_result 快照校验结果。
 */
void program_snapshot_capture(program_snapshot_t *program_snapshot, const char *program_id, int source_revision,
                              unsigned long captured_at_ms, const wash_program_t *wash_program,
                              program_snapshot_validation_t validation_result)
{
    if (program_snapshot == 0)
    {
        return;
    }

    program_snapshot_reset(program_snapshot);
    if (program_id != 0)
    {
        strncpy(program_snapshot->source_program_id, program_id, sizeof(program_snapshot->source_program_id) - 1);
    }
    /* 这些派生字段为后续追踪快照身份、版本和外部引用提供稳定落点。 */
    snprintf(program_snapshot->program_snapshot_id, sizeof(program_snapshot->program_snapshot_id), "snapshot-%lu",
             captured_at_ms);
    program_snapshot->source_revision = source_revision;
    program_snapshot->captured_at_ms = captured_at_ms;
    program_snapshot->validation_result = validation_result;
    snprintf(program_snapshot->snapshot_hash, sizeof(program_snapshot->snapshot_hash), "%s-%d-%lu",
             program_snapshot->source_program_id, source_revision, captured_at_ms);
    snprintf(program_snapshot->snapshot_payload_ref, sizeof(program_snapshot->snapshot_payload_ref), "program:%s@%d",
             program_snapshot->source_program_id, source_revision);
    if (wash_program != 0)
    {
        program_snapshot->frozen_program = *wash_program;
    }
}
