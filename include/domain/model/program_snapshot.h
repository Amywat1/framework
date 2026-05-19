#ifndef DOMAIN_MODEL_PROGRAM_SNAPSHOT_H
#define DOMAIN_MODEL_PROGRAM_SNAPSHOT_H

#include "domain/model/domain_enums.h"
#include "domain/model/wash_program.h"

/**
 * @file program_snapshot.h
 * @brief 定义会话绑定的程序快照。
 */
/**
 * @brief 描述一次冻结后的洗车程序快照。
 */
typedef struct program_snapshot_t
{
    /**< 程序快照 ID。 */
    char program_snapshot_id[32];
    /**< 源程序 ID。 */
    char source_program_id[32];
    /**< 源程序版本号。 */
    int source_revision;
    /**< 抓取时间。 */
    unsigned long captured_at_ms;
    /**< 快照校验结果。 */
    program_snapshot_validation_t validation_result;
    /**< 快照哈希。 */
    char snapshot_hash[64];
    /**< 快照内容引用。 */
    char snapshot_payload_ref[64];
    /**< 冻结后的程序内容。 */
    wash_program_v1_t frozen_program;
} program_snapshot_t;

/**
 * @brief 重置程序快照对象。
 *
 * @param program_snapshot 快照对象，不能为空。
 *
 * @note 快照对象只负责冻结程序配置，不负责写入最近结果或故障状态。
 */
void program_snapshot_reset(program_snapshot_t *program_snapshot);
/**
 * @brief 根据当前程序内容抓取并写入程序快照。
 * @param program_snapshot 快照对象，不能为空。
 * @param program_id 源程序 ID，可为空。
 * @param source_revision 源程序版本号。
 * @param captured_at_ms 抓取时间。
 * @param wash_program 源程序对象，不能为空。
 * @param validation_result 快照校验结果。
 */
void program_snapshot_capture(program_snapshot_t *program_snapshot, const char *program_id, int source_revision,
                              unsigned long captured_at_ms, const wash_program_t *wash_program,
                              program_snapshot_validation_t validation_result);

#endif
