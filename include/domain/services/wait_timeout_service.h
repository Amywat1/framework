#ifndef DOMAIN_SERVICES_WAIT_TIMEOUT_SERVICE_H
#define DOMAIN_SERVICES_WAIT_TIMEOUT_SERVICE_H

#include <stdbool.h>

#include "domain/model/wait_condition.h"
#include "shared/result_types.h"

/**
 * @file wait_timeout_service.h
 * @brief 定义超时判断服务。
 */
typedef struct wait_timeout_fact_t {
    bool timed_out;
    wait_timeout_policy_t timeout_policy;
    char reason_code[64];
} wait_timeout_fact_t;

bool wait_timeout_service_should_fire(const wait_condition_t *wait_condition, unsigned long current_time_ms);
operation_result_t wait_timeout_service_handle_timeout(const wait_condition_t *wait_condition,
    unsigned long current_time_ms,
    wait_timeout_fact_t *wait_timeout_fact);

#endif
