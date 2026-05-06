#ifndef DOMAIN_SERVICES_WASH_EXECUTION_SERVICE_H
#define DOMAIN_SERVICES_WASH_EXECUTION_SERVICE_H

#include <stdbool.h>

#include "application/coordinators/system_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file wash_execution_service.h
 * @brief 定义单次执行推进与等待装配规则。
 */
operation_result_t wash_execution_service_begin_next_stage(system_context_t *system_context);
operation_result_t wash_execution_service_handle_feedback(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event);
operation_result_t wash_execution_service_handle_stop(system_context_t *system_context, const char *reason_code);
operation_result_t wash_execution_service_handle_fault(system_context_t *system_context, const char *reason_code);
void wash_execution_service_log_retry(system_context_t *system_context, const char *reason_code);
bool wash_execution_service_is_waiting(const system_context_t *system_context);

#endif
