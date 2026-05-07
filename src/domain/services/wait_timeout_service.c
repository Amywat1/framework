#include "domain/services/wait_timeout_service.h"

#include <string.h>

#include "shared/error_codes.h"

static void write_reason_code(char *target, size_t target_size, const char *reason_code)
{
    if (target == 0 || target_size == 0) {
        return;
    }

    if (reason_code != 0 && reason_code[0] != '\0') {
        strncpy(target, reason_code, target_size - 1);
        target[target_size - 1] = '\0';
        return;
    }

    strncpy(target, "none", target_size - 1);
    target[target_size - 1] = '\0';
}

bool wait_timeout_service_should_fire(const wait_condition_t *wait_condition, unsigned long current_time_ms)
{
    if (wait_condition == 0) {
        return false;
    }

    return wait_condition_is_timed_out(wait_condition, current_time_ms);
}

operation_result_t wait_timeout_service_handle_timeout(wait_condition_t *wait_condition,
    unsigned long current_time_ms,
    wait_timeout_fact_t *wait_timeout_fact)
{
    unsigned long timeout_delta_ms;
    wait_timeout_policy_t timeout_policy;

    if (wait_condition == 0 || wait_timeout_fact == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    memset(wait_timeout_fact, 0, sizeof(*wait_timeout_fact));
    if (!wait_timeout_service_should_fire(wait_condition, current_time_ms)) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    timeout_delta_ms = wait_condition->timeout_at_ms - wait_condition->armed_at_ms;
    if (wait_condition_can_retry(wait_condition)) {
        wait_condition_rearm(wait_condition, current_time_ms, timeout_delta_ms);
        wait_timeout_fact->resolution = WAIT_TIMEOUT_RESOLUTION_RETRIED;
        wait_timeout_fact->retry_consumed = true;
        write_reason_code(wait_timeout_fact->reason_code,
            sizeof(wait_timeout_fact->reason_code),
            "timeout_retry");
        return operation_result_ok();
    }

    timeout_policy = wait_condition->timeout_policy;
    wait_condition_reset(wait_condition);
    if (timeout_policy == WAIT_TIMEOUT_POLICY_ABORT_SESSION) {
        wait_timeout_fact->resolution = WAIT_TIMEOUT_RESOLUTION_ABORT_SESSION;
        write_reason_code(wait_timeout_fact->reason_code,
            sizeof(wait_timeout_fact->reason_code),
            "timeout");
    } else if (timeout_policy == WAIT_TIMEOUT_POLICY_CONTINUE_SESSION) {
        wait_timeout_fact->resolution = WAIT_TIMEOUT_RESOLUTION_CONTINUE_SESSION;
        write_reason_code(wait_timeout_fact->reason_code,
            sizeof(wait_timeout_fact->reason_code),
            "timeout_continue");
    } else {
        wait_timeout_fact->resolution = WAIT_TIMEOUT_RESOLUTION_FINISH_EXECUTION;
        write_reason_code(wait_timeout_fact->reason_code,
            sizeof(wait_timeout_fact->reason_code),
            "timeout_finish");
    }

    return operation_result_ok();
}
