#ifndef DOMAIN_SERVICES_WAIT_TIMEOUT_SERVICE_H
#define DOMAIN_SERVICES_WAIT_TIMEOUT_SERVICE_H

#include <stdbool.h>

#include "domain/model/wait_condition.h"
#include "shared/result_types.h"

/**
 * @file wait_timeout_service.h
 * @brief 定义超时判断服务。
 */
/**
 * @brief 描述一次等待超时判定得到的事实。
 */
typedef struct wait_timeout_fact_t
{
    /**< 是否已经触发超时。 */
    bool timed_out;
    /**< 命中的超时策略。 */
    wait_timeout_policy_t timeout_policy;
    /**< 本次超时使用的原因码。 */
    char reason_code[64];
} wait_timeout_fact_t;

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
