#ifndef APPLICATION_COORDINATORS_WASH_CONTROL_OUTCOME_PROJECTION_H
#define APPLICATION_COORDINATORS_WASH_CONTROL_OUTCOME_PROJECTION_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file wash_control_outcome_projection.h
 * @brief 定义洗车控制结果在最近结果与迁移记录中的统一投影结构。
 */

/**
 * @brief 描述一次领域结论在最近结果与迁移记录中的统一投影。
 *
 * @note 该结构只承载最近一次对外投影，不承担会话终态存储职责。
 */
typedef struct wash_control_outcome_projection_t
{
    bool updates_latest_result;
    bool records_transition;
    transition_entity_type_t transition_entity;
    trigger_type_t trigger_type;
    char entity_id[32];
    char previous_state[32];
    char current_state[32];
    char latest_result_code[32];
    char latest_reason_code[64];
    char transition_result_code[32];
    char transition_reason_code[64];
} wash_control_outcome_projection_t;

/**
 * @brief 清空一份运行时结果投影结构。
 *
 * @param wash_control_outcome_projection 待初始化的投影对象，不能为空。
 */
void wash_control_outcome_projection_init(wash_control_outcome_projection_t *wash_control_outcome_projection);

/**
 * @brief 配置投影中的最近结果结论。
 *
 * @param wash_control_outcome_projection 投影对象，不能为空。
 * @param result_code 最近结果码；为空时写入 `"none"`。
 * @param reason_code 最近原因码；为空时写入 `"none"`。
 */
void wash_control_outcome_projection_set_latest_result(wash_control_outcome_projection_t *wash_control_outcome_projection,
                                                 const char *result_code, const char *reason_code);

/**
 * @brief 配置投影中的迁移记录结论。
 *
 * @param wash_control_outcome_projection 投影对象，不能为空。
 * @param transition_entity 迁移实体类型。
 * @param entity_id 迁移实体标识；为空时写入 `"none"`。
 * @param trigger_type 触发事件类型。
 * @param previous_state 前态；为空时写入 `"none"`。
 * @param current_state 后态；为空时写入 `"none"`。
 * @param result_code 迁移结果码；为空时写入 `"none"`。
 * @param reason_code 迁移原因码；为空时写入 `"none"`。
 */
void wash_control_outcome_projection_set_transition(wash_control_outcome_projection_t *wash_control_outcome_projection,
                                              transition_entity_type_t transition_entity, const char *entity_id,
                                              trigger_type_t trigger_type, const char *previous_state,
                                              const char *current_state, const char *result_code,
                                              const char *reason_code);

#endif
