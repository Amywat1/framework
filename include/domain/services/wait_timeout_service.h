#ifndef DOMAIN_SERVICES_WAIT_TIMEOUT_SERVICE_H
#define DOMAIN_SERVICES_WAIT_TIMEOUT_SERVICE_H

#include <stdbool.h>

#include "domain/model/wait_condition.h"
#include "shared/result_types.h"

/**
 * @file wait_timeout_service.h
 * @brief 定义超时判断服务。
 */
typedef struct wait_timeout_fact_t
{
    bool timed_out;
    wait_timeout_policy_t timeout_policy;
    char reason_code[64];
} wait_timeout_fact_t;

/**
 * @brief 判断当前等待条件是否已经超时。
 *
 * @param wait_condition 等待对象，不能为空。
 * @param current_time_ms 当前时间。
 * @return 已超时返回 `true`，否则返回 `false`。
 *
 * @note 该服务只产出超时事实，不直接改写执行或会话对象。
 */
bool wait_timeout_service_should_fire(const wait_condition_t *wait_condition, unsigned long current_time_ms);

/**
 * @brief 处理一次超时事实并输出结论。
 *
 * @param wait_condition 等待对象，不能为空。
 * @param current_time_ms 当前时间。
 * @param wait_timeout_fact 输出超时事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法或尚未超时时返回失败结果。
 */
operation_result_t wait_timeout_service_handle_timeout(const wait_condition_t *wait_condition,
                                                       unsigned long current_time_ms,
                                                       wait_timeout_fact_t *wait_timeout_fact);

#endif
