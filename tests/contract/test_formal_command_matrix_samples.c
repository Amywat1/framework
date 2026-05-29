#include "tests/test_support.h"

static int assert_device_state_equals(device_state_t expected_device_state)
{
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    result = query_wash_session_status( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.device_state == expected_device_state);
    return 0;
}

static int verify_stopped_start_returns_device_state_stopped(void)
{
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_control_context( &driver_context);
    result = test_load_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    result = test_process_command_and_flush(
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=device_state_stopped") != 0);
    TEST_ASSERT(assert_device_state_equals(DEVICE_STATE_STOPPED) == 0);

    test_release_control_context();
    return 0;
}

static int verify_idle_homing_returns_homing_requires_stopped(void)
{
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_control_context( &driver_context);
    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(assert_device_state_equals(DEVICE_STATE_IDLE) == 0);

    result = test_process_command_and_flush(
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=homing_requires_stopped") != 0);
    TEST_ASSERT(assert_device_state_equals(DEVICE_STATE_IDLE) == 0);

    test_release_control_context();
    return 0;
}

static int verify_running_stop_returns_manual_stop_and_stops_device(void)
{
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_control_context( &driver_context);
    result = test_load_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(assert_device_state_equals(DEVICE_STATE_RUNNING) == 0);

    result = test_process_command_and_flush( "stop", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "accepted=true") != 0);
    TEST_ASSERT(strstr(response_line, "detail=manual-stop") != 0);
    TEST_ASSERT(assert_device_state_equals(DEVICE_STATE_STOPPED) == 0);

    test_release_control_context();
    return 0;
}

static int verify_exception_fault_clear_returns_global_fault_cleared_and_stops_device(void)
{
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_control_context( &driver_context);
    result = test_process_command_and_flush(
        "fault E_STOP matrix-sample",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(assert_device_state_equals(DEVICE_STATE_EXCEPTION) == 0);

    result = test_process_command_and_flush(
        "fault clear",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=global_fault_cleared") != 0);
    TEST_ASSERT(assert_device_state_equals(DEVICE_STATE_STOPPED) == 0);

    test_release_control_context();
    return 0;
}

int main(void)
{
    if (verify_stopped_start_returns_device_state_stopped() != 0) {
        return 1;
    }
    if (verify_idle_homing_returns_homing_requires_stopped() != 0) {
        return 1;
    }
    if (verify_running_stop_returns_manual_stop_and_stops_device() != 0) {
        return 1;
    }
    if (verify_exception_fault_clear_returns_global_fault_cleared_and_stops_device() != 0) {
        return 1;
    }
    return 0;
}
