#ifndef APPLICATION_COORDINATORS_RUNTIME_EVENT_RECORDER_H
#define APPLICATION_COORDINATORS_RUNTIME_EVENT_RECORDER_H

#include "application/coordinators/system_context.h"
#include "domain/model/state_transition_record.h"

/**
 * @file runtime_event_recorder.h
 * @brief 定义运行时最近结果与事件记录辅助接口。
 */

/**
 * @brief 标识记录完成后需要写入的日志通道。
 */
typedef enum runtime_event_log_kind_t {
    RUNTIME_EVENT_LOG_NONE = 0,
    RUNTIME_EVENT_LOG_TRANSITION,
    RUNTIME_EVENT_LOG_REJECTION,
    RUNTIME_EVENT_LOG_IGNORED
} runtime_event_log_kind_t;

/**
 * @brief 刷新最近一次事件的结果码与原因码。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param result_code 最近结果码；传入 `0` 时保持现有值。
 * @param reason_code 最近原因码；传入 `0` 时保持现有值。
 */
void runtime_event_recorder_set_latest_result(system_context_t *system_context,
    const char *result_code,
    const char *reason_code);

/**
 * @brief 统一写入最近结果、最近迁移记录以及对应日志。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param transition_entity 迁移实体类型。
 * @param entity_id 迁移实体标识；为空时写入 `"none"`。
 * @param trigger_type 触发事件类型。
 * @param previous_state 前态；为空时写入 `"none"`。
 * @param current_state 后态；为空时写入 `"none"`。
 * @param result_code 事件结果码；为空时写入 `"none"`。
 * @param reason_code 事件原因码；为空时写入 `"none"`。
 * @param runtime_event_log_kind 对应日志通道。
 */
void runtime_event_recorder_record(system_context_t *system_context,
    transition_entity_type_t transition_entity,
    const char *entity_id,
    trigger_type_t trigger_type,
    const char *previous_state,
    const char *current_state,
    const char *result_code,
    const char *reason_code,
    runtime_event_log_kind_t runtime_event_log_kind);

#endif
