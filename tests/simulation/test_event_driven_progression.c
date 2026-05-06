#include "tests/test_support.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = main_loop_run(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_DEVICE_FEEDBACK, 0, "feedback:pre_soak", "sim-1", system_context.current_time_ms);
    result = main_loop_submit_trigger(&system_context, &wash_trigger_event);
    TEST_ASSERT(result.ok);
    result = main_loop_run(&system_context, 0);
    TEST_ASSERT(result.ok);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_DEVICE_FEEDBACK, 0, "feedback:wash", "sim-2", system_context.current_time_ms);
    result = main_loop_submit_trigger(&system_context, &wash_trigger_event);
    TEST_ASSERT(result.ok);
    result = main_loop_run(&system_context, 0);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_COMPLETED);
    return 0;
}
