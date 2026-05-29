#ifndef APPLICATION_USE_CASES_WASH_CONTROL_H
#define APPLICATION_USE_CASES_WASH_CONTROL_H

#include "application/coordinators/control_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file wash_control.h
 * @brief 定义统一触发处理用例。
 */

/**
 * @brief 分发并编排一次洗车控制触发（start/stop/homing/fault/timeout 等）。
 *
 * @param wash_trigger_event 当前待处理触发，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法或下游处理失败时返回失败结果。
 *
 * @note 本接口负责跨对象编排、`global_fault` / 最近结果最终落点与统一结果投影。
 * @note 会话、执行、等待条件和超时决议的具体推进由下游领域服务负责。
 */
operation_result_t dispatch_wash_control_trigger(const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 在运行中的洗车会话内推进程序工步执行一步。
 *
 * @return 成功返回 `operation_result_ok()`；上下文未激活或下游推进失败时返回失败结果。
 *
 * @note 本接口仅在会话运行中推进工步执行，并在需要时切换下一段或结束会话。
 */
operation_result_t advance_wash_session_program(void);

#endif
