#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

int main(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_RUNNING);
    TEST_ASSERT(driver_context.motion_command_count == 1);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);

    result = test_tick( 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_RUNNING);
    TEST_ASSERT(driver_context.motion_command_count == 1);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick( 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_EXITING);

    result = control_context_reset();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_NONE);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_PENDING);
    TEST_ASSERT(control_context_pending_trigger_count() == 0u);

    simulated_driver_context_init(&driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_RUNNING);
    TEST_ASSERT(driver_context.motion_command_count == 1);

    test_release_system_context();
    result = test_tick( 100);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

