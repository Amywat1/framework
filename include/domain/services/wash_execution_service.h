#ifndef DOMAIN_SERVICES_WASH_EXECUTION_SERVICE_H
#define DOMAIN_SERVICES_WASH_EXECUTION_SERVICE_H

#include <stdbool.h>

#include "application/coordinators/system_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file wash_execution_service.h
 * @brief 定义单次执行推进与等待装配规则。
 */

/**
 * @brief 启动下一阶段执行并装配新的等待条件。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法或下发失败时返回失败结果。
 */
operation_result_t wash_execution_service_begin_next_stage(system_context_t *system_context);

/**
 * @brief 处理一次匹配当前等待条件的设备反馈。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param wash_trigger_event 当前反馈触发，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法或反馈不匹配时返回失败结果。
 */
operation_result_t wash_execution_service_handle_feedback(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 处理中止当前执行的停止请求。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param reason_code 停止原因码。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_execution_service_handle_stop(system_context_t *system_context, const char *reason_code);

/**
 * @brief 处理导致当前执行终止的故障事件。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param reason_code 故障原因码。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_execution_service_handle_fault(system_context_t *system_context, const char *reason_code);

/**
 * @brief 按 timeout-continue 决议结束当前执行并准备切换下一阶段。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_execution_service_handle_timeout_continue(system_context_t *system_context);

/**
 * @brief 按 timeout-finish 决议结束当前执行。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_execution_service_handle_timeout_finish(system_context_t *system_context);

/**
 * @brief 按 timeout-abort 决议中止当前执行。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_execution_service_handle_timeout_abort(system_context_t *system_context);

/**
 * @brief 记录一次等待超时后的重试事件。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param reason_code 重试原因码。
 */
void wash_execution_service_log_retry(system_context_t *system_context, const char *reason_code);

/**
 * @brief 判断当前执行是否处于等待外部反馈状态。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @return 处于等待中返回 `true`，否则返回 `false`。
 */
bool wash_execution_service_is_waiting(const system_context_t *system_context);

#endif
