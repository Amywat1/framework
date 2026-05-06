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
bool wait_timeout_service_should_fire(const system_context_t *system_context);
operation_result_t wait_timeout_service_handle_timeout(system_context_t *system_context, wait_timeout_resolution_t *wait_timeout_resolution);

#endif
