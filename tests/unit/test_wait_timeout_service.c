#include "tests/test_support.h"

#include "domain/services/wait_timeout_service.h"

static int verify_retry_resolution(void)
{
    wait_condition_t wait_condition;
    wait_timeout_fact_t wait_timeout_fact;
    operation_result_t result;

    wait_condition_reset(&wait_condition);
    wait_condition_arm(&wait_condition,
        "exec-1",
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        "feedback:pre_soak",
        100,
        200,
        WAIT_TIMEOUT_POLICY_CONTINUE_SESSION,
        1,
        1);

    TEST_ASSERT(wait_timeout_service_should_fire(&wait_condition, 200));
    result = wait_timeout_service_handle_timeout(&wait_condition, 200, &wait_timeout_fact);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wait_timeout_fact.resolution == WAIT_TIMEOUT_RESOLUTION_RETRIED);
    TEST_ASSERT(wait_timeout_fact.retry_consumed == true);
    TEST_ASSERT(strcmp(wait_timeout_fact.reason_code, "timeout_retry") == 0);
    TEST_ASSERT(wait_condition.current_retry_count == 1);
    TEST_ASSERT(wait_condition.active == true);
    return 0;
}

static int verify_finish_resolution(void)
{
    wait_condition_t wait_condition;
    wait_timeout_fact_t wait_timeout_fact;
    operation_result_t result;

    wait_condition_reset(&wait_condition);
    wait_condition_arm(&wait_condition,
        "exec-2",
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        "feedback:wash",
        100,
        200,
        WAIT_TIMEOUT_POLICY_FINISH_EXECUTION,
        0,
        2);

    result = wait_timeout_service_handle_timeout(&wait_condition, 200, &wait_timeout_fact);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wait_timeout_fact.resolution == WAIT_TIMEOUT_RESOLUTION_FINISH_EXECUTION);
    TEST_ASSERT(strcmp(wait_timeout_fact.reason_code, "timeout_finish") == 0);
    TEST_ASSERT(wait_condition.active == false);
    return 0;
}

static int verify_abort_resolution(void)
{
    wait_condition_t wait_condition;
    wait_timeout_fact_t wait_timeout_fact;
    operation_result_t result;

    wait_condition_reset(&wait_condition);
    wait_condition_arm(&wait_condition,
        "exec-3",
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        "feedback:wash",
        100,
        200,
        WAIT_TIMEOUT_POLICY_ABORT_SESSION,
        0,
        3);

    result = wait_timeout_service_handle_timeout(&wait_condition, 200, &wait_timeout_fact);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wait_timeout_fact.resolution == WAIT_TIMEOUT_RESOLUTION_ABORT_SESSION);
    TEST_ASSERT(strcmp(wait_timeout_fact.reason_code, "timeout") == 0);
    TEST_ASSERT(wait_condition.active == false);
    return 0;
}

int main(void)
{
    if (verify_retry_resolution() != 0) {
        return 1;
    }
    if (verify_finish_resolution() != 0) {
        return 1;
    }
    if (verify_abort_resolution() != 0) {
        return 1;
    }
    return 0;
}
