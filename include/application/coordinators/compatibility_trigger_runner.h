#ifndef APPLICATION_COORDINATORS_COMPATIBILITY_TRIGGER_RUNNER_H
#define APPLICATION_COORDINATORS_COMPATIBILITY_TRIGGER_RUNNER_H

#include "application/coordinators/system_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file compatibility_trigger_runner.h
 * @brief 定义兼容性写入入口复用的 trigger 入队执行辅助接口。
 */

/**
 * @brief 以兼容性包装方式执行单个 trigger，但仍复用正式主循环消费链路。
 *
 * @param system_context 主控共享上下文，不能为 `0`。
 * @param wash_trigger_event 待执行 trigger；不能为 `0`。
 * @return 执行成功返回 `operation_result_ok()`；参数非法、入队失败或主循环处理失败时返回失败结果。
 *
 * @note 该接口仅供保留的兼容 use case 包装复用，不得作为新的正式产品入口对外扩散。
 */
operation_result_t compatibility_trigger_runner_execute(system_context_t *system_context,
    const wash_trigger_event_t *wash_trigger_event);

#endif
