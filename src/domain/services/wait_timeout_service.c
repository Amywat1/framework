#include "domain/services/wait_timeout_service.h"

#include "domain/services/wash_execution_service.h"
#include "shared/error_codes.h"

bool wait_timeout_service_should_fire(const system_context_t *system_context)
{
    if (system_context == 0) {
        return false;
    }
    return wait_condition_is_timed_out(&system_context->wait_condition, system_context->current_time_ms);
}

operation_result_t wait_timeout_service_handle_timeout(system_context_t *system_context, wait_timeout_resolution_t *wait_timeout_resolution)
{
    unsigned long timeout_delta_ms;
    wait_timeout_policy_t timeout_policy;

    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (wait_timeout_resolution == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    *wait_timeout_resolution = WAIT_TIMEOUT_RESOLUTION_NONE;
    if (!wait_timeout_service_should_fire(system_context)) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    timeout_delta_ms = system_context->wait_condition.timeout_at_ms - system_context->wait_condition.armed_at_ms;
    if (wait_condition_can_retry(&system_context->wait_condition)) {
        wait_condition_rearm(&system_context->wait_condition, system_context->current_time_ms, timeout_delta_ms);
        wash_execution_service_log_retry(system_context, "timeout_retry");
        *wait_timeout_resolution = WAIT_TIMEOUT_RESOLUTION_RETRIED;
        return operation_result_ok();
    }

    timeout_policy = system_context->wait_condition.timeout_policy;
    wait_condition_reset(&system_context->wait_condition);
    if (timeout_policy == WAIT_TIMEOUT_POLICY_ABORT_SESSION) {
        *wait_timeout_resolution = WAIT_TIMEOUT_RESOLUTION_ABORT_SESSION;
    } else {
        *wait_timeout_resolution = timeout_policy == WAIT_TIMEOUT_POLICY_CONTINUE_SESSION
            ? WAIT_TIMEOUT_RESOLUTION_CONTINUE_SESSION
            : WAIT_TIMEOUT_RESOLUTION_FINISH_EXECUTION;
    }
    return operation_result_ok();
}
