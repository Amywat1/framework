#include "domain/services/wait_timeout_service.h"

#include <string.h>

#include "shared/error_codes.h"

operation_result_t wait_timeout_service_handle_timeout(const wait_condition_t *wait_condition,
                                                       unsigned long current_time_ms,
                                                       wait_timeout_fact_t *wait_timeout_fact)
{
    if (wait_condition == 0 || wait_timeout_fact == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    memset(wait_timeout_fact, 0, sizeof(*wait_timeout_fact));
    if (!wait_condition_is_timed_out(wait_condition, current_time_ms))
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wait_timeout_fact->timed_out = true;
    wait_timeout_fact->timeout_policy = wait_condition->timeout_policy;
    strncpy(wait_timeout_fact->reason_code, wait_condition->reason_code, sizeof(wait_timeout_fact->reason_code) - 1);
    return operation_result_ok();
}
