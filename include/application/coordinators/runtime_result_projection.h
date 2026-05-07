#ifndef APPLICATION_COORDINATORS_RUNTIME_RESULT_PROJECTION_H
#define APPLICATION_COORDINATORS_RUNTIME_RESULT_PROJECTION_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file runtime_result_projection.h
 * @brief 定义应用层统一运行时结果投影结构。
 */

/**
 * @brief 标识投影完成后需要写入的日志通道。
 */
typedef enum runtime_event_log_kind_t {
    RUNTIME_EVENT_LOG_NONE = 0,
    RUNTIME_EVENT_LOG_TRANSITION,
    RUNTIME_EVENT_LOG_REJECTION,
    RUNTIME_EVENT_LOG_IGNORED
} runtime_event_log_kind_t;

/**
 * @brief 描述一次领域结论在最近结果、迁移记录和日志中的统一投影。
 */
typedef struct runtime_result_projection_t {
    bool updates_latest_result;
    bool records_transition;
    transition_entity_type_t transition_entity;
    trigger_type_t trigger_type;
    runtime_event_log_kind_t log_kind;
    char entity_id[32];
    char previous_state[32];
    char current_state[32];
    char latest_result_code[32];
    char latest_reason_code[64];
    char transition_result_code[32];
    char transition_reason_code[64];
} runtime_result_projection_t;

/**
 * @brief 清空一份运行时结果投影结构。
 *
 * @param runtime_result_projection 待初始化的投影对象，不能为空。
 */
void runtime_result_projection_init(runtime_result_projection_t *runtime_result_projection);

/**
 * @brief 配置投影中的最近结果结论。
 *
 * @param runtime_result_projection 投影对象，不能为空。
 * @param result_code 最近结果码；为空时写入 `"none"`。
 * @param reason_code 最近原因码；为空时写入 `"none"`。
 */
void runtime_result_projection_set_latest_result(runtime_result_projection_t *runtime_result_projection,
    const char *result_code,
    const char *reason_code);

/**
 * @brief 配置投影中的迁移记录与日志结论。
 *
 * @param runtime_result_projection 投影对象，不能为空。
 * @param transition_entity 迁移实体类型。
 * @param entity_id 迁移实体标识；为空时写入 `"none"`。
 * @param trigger_type 触发事件类型。
 * @param previous_state 前态；为空时写入 `"none"`。
 * @param current_state 后态；为空时写入 `"none"`。
 * @param result_code 迁移结果码；为空时写入 `"none"`。
 * @param reason_code 迁移原因码；为空时写入 `"none"`。
 * @param runtime_event_log_kind 对应日志通道。
 */
void runtime_result_projection_set_transition(runtime_result_projection_t *runtime_result_projection,
    transition_entity_type_t transition_entity,
    const char *entity_id,
    trigger_type_t trigger_type,
    const char *previous_state,
    const char *current_state,
    const char *result_code,
    const char *reason_code,
    runtime_event_log_kind_t runtime_event_log_kind);

#endif
