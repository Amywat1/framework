#ifndef PLATFORM_LINUX_MAIN_LOOP_H
#define PLATFORM_LINUX_MAIN_LOOP_H

#include "application/coordinators/system_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file main_loop.h
 * @brief 定义 Linux 主控循环的共享入口。
 */

/**
 * @brief 推进一轮主循环处理。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法或处理失败时返回失败结果。
 */
operation_result_t main_loop_run(system_context_t *system_context);

/**
 * @brief 向主循环挂起队列提交一个待处理触发事件。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param wash_trigger_event 待提交的触发事件，不能为空。
 * @return 提交成功返回 `operation_result_ok()`；队列满或参数非法时返回失败结果。
 */
operation_result_t main_loop_submit_trigger(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 推进主控内部时间基准。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param elapsed_ms 需要推进的毫秒数。
 */
void main_loop_advance_time(system_context_t *system_context, unsigned long elapsed_ms);

#endif
