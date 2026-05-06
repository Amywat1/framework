#ifndef DOMAIN_SERVICES_WAIT_TIMEOUT_SERVICE_H
#define DOMAIN_SERVICES_WAIT_TIMEOUT_SERVICE_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

typedef enum wait_timeout_resolution_t {
    WAIT_TIMEOUT_RESOLUTION_NONE = 0,
    WAIT_TIMEOUT_RESOLUTION_RETRIED,
    WAIT_TIMEOUT_RESOLUTION_CONTINUE_SESSION,
    WAIT_TIMEOUT_RESOLUTION_FINISH_EXECUTION,
    WAIT_TIMEOUT_RESOLUTION_ABORT_SESSION
} wait_timeout_resolution_t;

/**
 * @file wait_timeout_service.h
 * @brief 定义等待超时判定与重试规则。
 */

/**
 * @brief 判断当前等待条件是否已经触发超时。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @return 超时返回 `true`，否则返回 `false`。
 */
bool wait_timeout_service_should_fire(const system_context_t *system_context);

/**
 * @brief 处理一次等待超时，并给出后续收敛决议。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param wait_timeout_resolution 输出超时决议，不能为空。
 * @return 处理成功返回 `operation_result_ok()`；参数非法或当前未超时时返回失败结果。
 */
operation_result_t wait_timeout_service_handle_timeout(system_context_t *system_context, wait_timeout_resolution_t *wait_timeout_resolution);

#endif
