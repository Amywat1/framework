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
 *
 * @note 本接口只负责选择和消费待处理 trigger，不直接实现业务状态写入策略。
 */
operation_result_t main_loop_run(system_context_t *system_context);

/**
 * @brief 向主循环挂起队列提交一个待处理触发事件。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param wash_trigger_event 待提交的触发事件，不能为空。
 * @return 提交成功返回 `operation_result_ok()`；队列满或参数非法时返回失败结果。
 *
 * @note 这是外部路径提交触发的唯一正式入口；适配层和领域服务不得直接改写队列字段。
 */
operation_result_t main_loop_submit_trigger(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 推进主控内部时间基准。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param elapsed_ms 需要推进的毫秒数。
 *
 * @note 当前时间属于主循环运行事实，只允许通过本接口推进。
 */
void main_loop_advance_time(system_context_t *system_context, unsigned long elapsed_ms);

#endif
