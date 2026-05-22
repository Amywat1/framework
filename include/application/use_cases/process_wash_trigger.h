#ifndef APPLICATION_USE_CASES_PROCESS_WASH_TRIGGER_H
#define APPLICATION_USE_CASES_PROCESS_WASH_TRIGGER_H

#include "application/coordinators/device_runtime.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file process_wash_trigger.h
 * @brief 定义统一触发处理用例。
 */

/**
 * @brief 统一路由并编排一次待处理触发事件。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param wash_trigger_event 当前待处理触发，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法或下游处理失败时返回失败结果。
 *
 * @note 本接口负责跨对象编排、`global_fault` / 最近结果最终落点与统一结果投影。
 * @note 会话、执行、等待条件和超时决议的具体推进由下游领域服务负责。
 */
operation_result_t process_wash_trigger_execute(device_runtime_t system_context,
                                                const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 推进一次运行中会话的 runtime tick。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @return 成功返回 `operation_result_ok()`；上下文非法或下游推进失败时返回失败结果。
 *
 * @note 本接口仅在会话运行中推进执行状态，并在需要时驱动会话收尾。
 */
operation_result_t process_wash_runtime_tick(device_runtime_t system_context);

#endif
