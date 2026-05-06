#ifndef APPLICATION_USE_CASES_STOP_WASH_CYCLE_H
#define APPLICATION_USE_CASES_STOP_WASH_CYCLE_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

/**
 * @file stop_wash_cycle.h
 * @brief 定义洗车停止用例。
 */

/**
 * @brief 以默认原因请求停止当前洗车任务。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @return 触发成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t stop_wash_cycle_execute(system_context_t *system_context);

/**
 * @brief 以显式原因请求停止当前洗车任务。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param reason_code 停止原因，允许为空时由下游按默认值处理。
 * @return 触发成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t stop_wash_cycle_with_reason_execute(system_context_t *system_context, const char *reason_code);

#endif
