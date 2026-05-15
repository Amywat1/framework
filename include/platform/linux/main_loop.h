#ifndef PLATFORM_LINUX_MAIN_LOOP_H
#define PLATFORM_LINUX_MAIN_LOOP_H

#include "application/coordinators/system_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file main_loop.h
 * @brief 声明主控单拍运行时入口。
 */

/**
 * @brief 执行一次有界的主控单拍推进。
 *
 * @param system_context 主控运行时组合根句柄。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 *
 * @note 该函数不等待任何操作系统事件，也不拥有外层事件循环；何时调用由平台调度器决定。
 */
operation_result_t main_loop_run(system_context_t system_context);

/**
 * @brief 向运行时待处理队列提交一个触发事件。
 *
 * @param system_context 主控运行时组合根句柄。
 * @param wash_trigger_event 待入队的触发事件。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t main_loop_submit_trigger(system_context_t system_context,
                                            const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 按给定时长推进主控当前时间。
 *
 * @param system_context 主控运行时组合根句柄。
 * @param elapsed_ms 本次推进的毫秒数。
 *
 * @note 稳态运行时的时间基准由平台调度器拥有，常规情况下只应由调度器调用此接口。
 */
void main_loop_advance_time(system_context_t system_context, unsigned long elapsed_ms);

#endif
