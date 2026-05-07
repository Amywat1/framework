#ifndef APPLICATION_USE_CASES_ACKNOWLEDGE_FAULT_H
#define APPLICATION_USE_CASES_ACKNOWLEDGE_FAULT_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

/**
 * @file acknowledge_fault.h
 * @brief 定义故障上报与清除用例。
 */

/**
 * @brief 提交一次故障上报，或在特定参数下请求清除全局故障。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param fault_code 故障代码；当值为 `clear` 时表示清除全局故障。
 * @param fault_reason 故障原因；清除全局故障时可传入 `0`。
 * @return 执行成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 *
 * @note 本接口保留为兼容性辅助包装，新代码不应把它作为生产主路径入口。
 */
operation_result_t acknowledge_fault_execute(system_context_t *system_context,
    const char *fault_code,
    const char *fault_reason);

#endif
