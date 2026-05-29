#ifndef DOMAIN_SERVICES_FAULT_POLICY_SERVICE_H
#define DOMAIN_SERVICES_FAULT_POLICY_SERVICE_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"
#include "shared/result_types.h"

/**
 * @file fault_policy_service.h
 * @brief 定义异常策略决议服务。
 */
/**
 * @brief 描述一次异常策略求值得到的决议。
 */
typedef struct fault_policy_decision_t
{
    /**< 是否需要直接中止会话。 */
    bool abort_session;
    /**< 是否需要先进入退出流程。 */
    bool enter_exit;
} fault_policy_decision_t;

/**
 * @brief 根据异常策略和当前阶段求出处理决议。
 * @param exception_strategy 当前异常策略。
 * @param currently_exiting 当前是否已处于退出阶段。
 * @param fault_policy_decision 输出决议结果。
 * @return 成功返回 `operation_result_ok()`，参数非法时返回失败结果。
 */
operation_result_t fault_policy_service_resolve(exception_strategy_t exception_strategy, bool currently_exiting,
                                                fault_policy_decision_t *fault_policy_decision);

#endif
