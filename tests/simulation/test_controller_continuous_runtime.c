#include "tests/test_support.h"

static int verify_idle_runtime_keeps_waiting(void)
{
    int index;
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    for (index = 0; index < 16; ++index) {
        result = main_loop_run(&system_context);
        TEST_ASSERT(result.ok);
        TEST_ASSERT(system_context.pending_trigger_count == 0);
        TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_NONE);
    }
    return 0;
}

static int verify_runtime_after_one_session(void)
{
    int index;
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_DEVICE_FEEDBACK, 0, "feedback:pre_soak", "continuous-1", 1);
    TEST_ASSERT(main_loop_submit_trigger(&system_context, &wash_trigger_event).ok);
    TEST_ASSERT(main_loop_run(&system_context).ok);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_DEVICE_FEEDBACK, 0, "feedback:wash", "continuous-2", 1);
    TEST_ASSERT(main_loop_submit_trigger(&system_context, &wash_trigger_event).ok);
    TEST_ASSERT(main_loop_run(&system_context).ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_COMPLETED);

    for (index = 0; index < 8; ++index) {
        result = main_loop_run(&system_context);
        TEST_ASSERT(result.ok);
        TEST_ASSERT(system_context.pending_trigger_count == 0);
    }
    return 0;
}

int main(void)
{
    if (verify_idle_runtime_keeps_waiting() != 0) {
        return 1;
    }
    if (verify_runtime_after_one_session() != 0) {
        return 1;
    }
    return 0;
}
