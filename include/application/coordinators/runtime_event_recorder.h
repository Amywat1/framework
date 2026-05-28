#ifndef APPLICATION_COORDINATORS_RUNTIME_EVENT_RECORDER_H
#define APPLICATION_COORDINATORS_RUNTIME_EVENT_RECORDER_H

#include "application/coordinators/runtime_result_projection.h"
#include "application/coordinators/control_context.h"
#include "domain/model/state_transition_record.h"

/**
 * @file runtime_event_recorder.h
 * @brief 定义运行时最近结果与事件记录辅助接口。
 */

/**
 * @brief 刷新最近一次事件的结果码与原因码。
 *
 * @param result_code 最近结果码；传入 `0` 时保持现有值。
 * @param reason_code 最近原因码；传入 `0` 时保持现有值。
 *
 * @note 本接口只维护最近一次对外投影，不解释会话最终结论。
 */
void runtime_event_recorder_set_latest_result(const char *result_code, const char *reason_code);

/**
 * @brief 应用统一运行时结果投影。
 *
 * @param runtime_result_projection 统一运行时结果投影，不能为空。
 *
 * @note 本接口是最近结果与迁移记录的唯一正式落点，不负责修改会话最终结果。
 */
void runtime_event_recorder_apply_projection(const runtime_result_projection_t *runtime_result_projection);

#endif
