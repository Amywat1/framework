#include "tests/test_support.h"

static int verify_fault_priority(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_trigger_event_t feedback_event;
    wash_trigger_event_t stop_event;
    wash_trigger_event_t fault_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = main_loop_run(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    wash_trigger_event_init(&feedback_event, TRIGGER_TYPE_DEVICE_FEEDBACK, 0, "feedback:pre_soak", "dup-1", 1);
    wash_trigger_event_init(&stop_event, TRIGGER_TYPE_STOP, 0, "sim-stop", "dup-2", 1);
    wash_trigger_event_init(&fault_event, TRIGGER_TYPE_FAULT, 0, "sim-fault", "dup-3", 1);
    TEST_ASSERT(main_loop_submit_trigger(&system_context, &feedback_event).ok);
    TEST_ASSERT(main_loop_submit_trigger(&system_context, &stop_event).ok);
    TEST_ASSERT(main_loop_submit_trigger(&system_context, &fault_event).ok);

    result = main_loop_run(&system_context, 0);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(system_context.wash_session.abort_reason, "sim-fault") == 0);

    result = test_submit_feedback(&system_context, "feedback:pre_soak", "dup-3");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.last_transition_record.reason_code, "late_event") == 0
        || strcmp(system_context.last_transition_record.reason_code, "duplicate_event") == 0);
    return 0;
}

static int verify_feedback_survives_timeout_round(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_trigger_event_t feedback_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = main_loop_run(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    system_context.wait_condition.max_retry_count = 1;
    wash_trigger_event_init(&feedback_event, TRIGGER_TYPE_DEVICE_FEEDBACK, 0, "feedback:pre_soak", "keep-feedback", 1);
    TEST_ASSERT(main_loop_submit_trigger(&system_context, &feedback_event).ok);
    main_loop_advance_time(&system_context, 1000);

    result = main_loop_run(&system_context, 0);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wait_condition.current_retry_count == 1);
    TEST_ASSERT(system_context.pending_trigger_count == 1);

    result = main_loop_run(&system_context, 0);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.wash_session.progress_stage_id, "wash") == 0);
    return 0;
}

int main(void)
{
    if (verify_fault_priority() != 0) {
        return 1;
    }
    if (verify_feedback_survives_timeout_round() != 0) {
        return 1;
    }
    return 0;
}
