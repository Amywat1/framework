#include "application/use_cases/formal_command.h"
#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

int main(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_control_context( &driver_context);
    result = test_load_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);

    result = formal_command_execute(
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "detail=queued") != 0);
    TEST_ASSERT(control_context_pending_trigger_count() == 1u);
    TEST_ASSERT(control_context_private_wash_session()->session_state != SESSION_STATE_RUNNING);

    control_tick_advance_time( 100ul);
    TEST_ASSERT(control_context_current_time_ms() == 100ul);
    result = control_tick_run();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_pending_trigger_count() == 0u);
    TEST_ASSERT(control_context_private_wash_session()->session_state == SESSION_STATE_RUNNING);
    return 0;
}

