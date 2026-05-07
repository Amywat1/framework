#ifndef DOMAIN_SERVICES_WAIT_TIMEOUT_SERVICE_H
#define DOMAIN_SERVICES_WAIT_TIMEOUT_SERVICE_H

#include <stdbool.h>

#include "domain/model/wait_condition.h"
#include "shared/result_types.h"

/**
 * @file wait_timeout_service.h
 * @brief 定义等待超时判定与重试规则。
 */

/**
 * @brief 定义等待超时后的后续决议类型。
 */
typedef enum wait_timeout_resolution_t {
    WAIT_TIMEOUT_RESOLUTION_NONE = 0,
    WAIT_TIMEOUT_RESOLUTION_RETRIED,
    WAIT_TIMEOUT_RESOLUTION_CONTINUE_SESSION,
    WAIT_TIMEOUT_RESOLUTION_FINISH_EXECUTION,
    WAIT_TIMEOUT_RESOLUTION_ABORT_SESSION
} wait_timeout_resolution_t;

/**
 * @brief 描述等待超时服务返回给应用层的决议事实。
 */
typedef struct wait_timeout_fact_t {
    wait_timeout_resolution_t resolution;
    char reason_code[64];
    bool retry_consumed;
} wait_timeout_fact_t;

/**
 * @brief 判断当前等待条件是否已经触发超时。
 *
 * @param wait_condition 当前等待条件，不能为空。
 * @param current_time_ms 当前时间戳。
 * @return 超时返回 `true`，否则返回 `false`。
 */
bool wait_timeout_service_should_fire(const wait_condition_t *wait_condition, unsigned long current_time_ms);

/**
 * @brief 处理一次等待超时，并给出后续收敛决议。
 *
 * @param wait_condition 当前等待条件，不能为空。
 * @param current_time_ms 当前时间戳。
 * @param wait_timeout_fact 输出超时决议事实，不能为空。
 * @return 处理成功返回 `operation_result_ok()`；参数非法或当前未超时时返回失败结果。
 *
 * @note 本接口只负责 timeout 决策与必要的等待条件更新，不直接推进会话或执行状态机。
 */
operation_result_t wait_timeout_service_handle_timeout(wait_condition_t *wait_condition,
    unsigned long current_time_ms,
    wait_timeout_fact_t *wait_timeout_fact);

#endif
