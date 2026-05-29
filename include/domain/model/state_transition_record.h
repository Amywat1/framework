#ifndef DOMAIN_MODEL_STATE_TRANSITION_RECORD_H
#define DOMAIN_MODEL_STATE_TRANSITION_RECORD_H

#include "domain/model/domain_enums.h"

/**
 * @file state_transition_record.h
 * @brief 定义状态迁移与拒绝/忽略记录。
 */
/**
 * @brief 描述一次状态迁移、拒绝或忽略记录。
 */
typedef struct state_transition_record_t
{
    /**< 记录 ID。 */
    char record_id[32];
    /**< 迁移实体类型。 */
    transition_entity_type_t entity_type;
    /**< 迁移实体 ID。 */
    char entity_id[32];
    /**< 关联触发类型。 */
    trigger_type_t trigger_type;
    /**< 变更前状态。 */
    char previous_state[32];
    /**< 变更后状态。 */
    char current_state[32];
    /**< 结果码。 */
    char result_code[32];
    /**< 原因码。 */
    char reason_code[64];
    /**< 记录时间。 */
    unsigned long recorded_at_ms;
} state_transition_record_t;

/**
 * @brief 初始化一条状态迁移记录。
 * @param state_transition_record 记录对象，不能为空。
 * @param entity_type 迁移实体类型。
 * @param entity_id 迁移实体 ID，可为空。
 * @param trigger_type 关联触发类型。
 * @param previous_state 变更前状态，可为空。
 * @param current_state 变更后状态，可为空。
 * @param result_code 结果码，可为空。
 * @param reason_code 原因码，可为空。
 * @param recorded_at_ms 记录时间。
 */
void state_transition_record_init(state_transition_record_t *state_transition_record,
                                  transition_entity_type_t entity_type, const char *entity_id,
                                  trigger_type_t trigger_type, const char *previous_state, const char *current_state,
                                  const char *result_code, const char *reason_code, unsigned long recorded_at_ms);

#endif
