#ifndef APPLICATION_USE_CASES_START_WASH_CYCLE_H
#define APPLICATION_USE_CASES_START_WASH_CYCLE_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

/**
 * @file start_wash_cycle.h
 * @brief 定义洗车启动用例。
 */

/**
 * @brief 受理一次新的洗车启动请求。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param program_id 启动所选程序标识，不能为空字符串。
 * @return 受理成功返回 `operation_result_ok()`；预检失败或参数非法时返回失败结果。
 */
operation_result_t start_wash_cycle_execute(system_context_t *system_context, const char *program_id);

#endif
