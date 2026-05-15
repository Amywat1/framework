#ifndef DOMAIN_SERVICES_FAULT_POLICY_SERVICE_H
#define DOMAIN_SERVICES_FAULT_POLICY_SERVICE_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"
#include "shared/result_types.h"

/**
 * @file fault_policy_service.h
 * @brief 定义异常策略决议服务。
 */
typedef struct fault_policy_decision_t
{
    bool abort_session;
    bool enter_exit;
} fault_policy_decision_t;

operation_result_t fault_policy_service_resolve(exception_strategy_t exception_strategy, bool currently_exiting,
                                                fault_policy_decision_t *fault_policy_decision);

#endif
