#include "application/use_cases/process_wash_trigger.h"
#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

typedef struct homing_alarm_injection_context_t {
    system_context_t system_context;
    actuator_port_t actuator_port;
    sensor_port_t sensor_port;
} homing_alarm_injection_context_t;

static int homing_alarm_injection_stop_all(void *context, int timeout_ms)
{
    homing_alarm_injection_context_t *homing_alarm_injection_context;
    int result;

    homing_alarm_injection_context = context;
    result = homing_alarm_injection_context->actuator_port.stop_all(
        homing_alarm_injection_context->actuator_port.context,
        timeout_ms);
    if (result == 0) {
        system_context_private_set_global_fault(homing_alarm_injection_context->system_context,
            "RECOVERING_FAULT",
            "recovering-alarm");
    }
    return result;
}

static int homing_alarm_injection_home_roof_brush(void *context, int timeout_ms)
{
    homing_alarm_injection_context_t *homing_alarm_injection_context;

    homing_alarm_injection_context = context;
    return homing_alarm_injection_context->actuator_port.home_roof_brush(
        homing_alarm_injection_context->actuator_port.context,
        timeout_ms);
}

static int homing_alarm_injection_read_runtime_snapshot(void *context, runtime_snapshot_t *runtime_snapshot)
{
    homing_alarm_injection_context_t *homing_alarm_injection_context;

    homing_alarm_injection_context = context;
    return homing_alarm_injection_context->sensor_port.read_runtime_snapshot(
        homing_alarm_injection_context->sensor_port.context,
        runtime_snapshot);
}

static int verify_power_on_defaults_to_stopped_and_blocks_start(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_STOPPED);

    result = test_process_command_and_flush(system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "device_state_stopped") != 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "device_state_stopped") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "device_state_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_rejected_from_idle(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=homing_requires_stopped") != 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "homing_requires_stopped") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "homing_requires_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_HOMING);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

static int verify_stop_rejected_from_stopped_and_idle(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = test_process_command_and_flush(system_context,
        "stop",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=stop_requires_running") != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = test_process_command_and_flush(system_context,
        "stop",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=stop_requires_running") != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

static int verify_fault_clear_rejected_from_stopped_and_idle(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = test_process_command_and_flush(system_context,
        "fault clear",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=fault_clear_requires_exception") != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = test_process_command_and_flush(system_context,
        "fault clear",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=fault_clear_requires_exception") != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_clears_alarm_before_recovering(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    system_context_private_set_global_fault(system_context, "STALE_FAULT", "stale-alarm");
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!system_context_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(test_latest_reason_code(system_context), "homing_completed") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "homing_completed") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_HOMING);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_success_runs_real_flow_and_returns_to_idle(void)
{
    int final_segment_index;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(test_latest_reason_code(system_context), "homing_completed") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "homing_completed") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_HOMING);

    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_RUNNING);

    final_segment_index = system_context_private_runtime(system_context)->program_snapshot.frozen_program.segment_count - 1;
    system_context_private_runtime(system_context)->wash_execution.segment_index = final_segment_index;
    system_context_private_runtime(system_context)->wash_execution.execution_state = EXECUTION_STATE_COMPLETED;
    system_context_private_runtime(system_context)->wash_execution.lifecycle_state = SEGMENT_LIFECYCLE_COMPLETED;

    result = process_wash_runtime_tick(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_COMPLETED);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_alarm_during_recovering_moves_device_to_exception(void)
{
    actuator_port_t actuator_port;
    char response_line[512];
    homing_alarm_injection_context_t homing_alarm_injection_context;
    sensor_port_t sensor_port;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    memset(&homing_alarm_injection_context, 0, sizeof(homing_alarm_injection_context));
    homing_alarm_injection_context.system_context = system_context;
    homing_alarm_injection_context.actuator_port = system_context_private_runtime(system_context)->actuator_port;
    homing_alarm_injection_context.sensor_port = system_context_private_runtime(system_context)->sensor_port;

    actuator_port = homing_alarm_injection_context.actuator_port;
    actuator_port.context = &homing_alarm_injection_context;
    actuator_port.stop_all = homing_alarm_injection_stop_all;
    actuator_port.home_roof_brush = homing_alarm_injection_home_roof_brush;
    system_context_set_actuator_port(system_context, &actuator_port);

    sensor_port = homing_alarm_injection_context.sensor_port;
    sensor_port.context = &homing_alarm_injection_context;
    sensor_port.read_runtime_snapshot = homing_alarm_injection_read_runtime_snapshot;
    system_context_set_sensor_port(system_context, &sensor_port);

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=recovering_alarm_triggered") != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_stop_all_failure_moves_device_to_exception(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.stop_all_should_fail = true;

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=stop_all_failed") != 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 0);
    TEST_ASSERT(driver_context.roof_home_command_count == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_roof_home_command_failure_moves_device_to_exception(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.roof_home_command_should_fail = true;

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=roof_home_command_failed") != 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_snapshot_failure_moves_device_to_exception(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.runtime_snapshot_read_should_fail = true;

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=runtime_snapshot_unavailable") != 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_feedback_timeout_moves_device_to_exception(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.roof_home_feedback_available = false;

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=roof_home_feedback_timeout") != 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_not_reached_moves_device_to_exception(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.roof_home_feedback_available = true;
    driver_context.roof_home_reached = false;

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=roof_home_not_reached") != 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_running_manual_stop_moves_device_to_stopped(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_RUNNING);

    result = test_submit_stop(system_context, "device-stop");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    test_release_system_context(system_context);
    return 0;
}

static int verify_trigger_start_rejected_from_stopped_with_matrix_reason(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_START,
        "wash_step_control_v1",
        0,
        "trigger-start",
        system_context_current_time_ms(system_context));
    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "device_state_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_START);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "device_state_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    test_release_system_context(system_context);
    return 0;
}

static int verify_trigger_start_rejected_from_exception_with_matrix_reason(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_submit_fault_with_reason(system_context, "E_STOP", "trigger-exception");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_START,
        "wash_step_control_v1",
        0,
        "trigger-start",
        system_context_current_time_ms(system_context));
    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "device_state_exception") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_START);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "device_state_exception") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);

    test_release_system_context(system_context);
    return 0;
}

/*
 * @brief 楠岃瘉缁熶竴瑙﹀彂鍏ュ彛鐨?homing 鍦?IDLE 鍜?EXCEPTION 涓嬮兘鎸夌煩闃垫嫆缁濄€? */
static int verify_trigger_homing_rejected_from_idle_and_exception(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_HOMING,
        0,
        "homing",
        "trigger-homing",
        system_context_current_time_ms(system_context));
    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "homing_requires_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_HOMING);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "homing_requires_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = test_submit_fault_with_reason(system_context, "E_STOP", "trigger-homing-exception");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "homing_requires_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_HOMING);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "homing_requires_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

/**
 * @brief 楠岃瘉缁熶竴瑙﹀彂鍏ュ彛鐨?stop 鍦?STOPPED 鍜?IDLE 涓嬮兘鎸夌煩闃垫嫆缁濄€? */
static int verify_trigger_stop_rejected_from_stopped_and_idle(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_STOP,
        0,
        "trigger-stop",
        "trigger-stop",
        system_context_current_time_ms(system_context));
    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "stop_requires_running") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_STOP);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "stop_requires_running") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "stop_requires_running") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_STOP);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "stop_requires_running") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

/**
 * @brief 楠岃瘉缁熶竴瑙﹀彂鍏ュ彛鐨?fault clear 鍦?STOPPED 鍜?IDLE 涓嬮兘鎸夌煩闃垫嫆缁濄€? */
static int verify_trigger_fault_clear_rejected_from_stopped_and_idle(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_FAULT,
        0,
        "clear",
        0,
        system_context_current_time_ms(system_context));
    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "fault_clear_requires_exception") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "fault_clear_requires_exception") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "fault_clear_requires_exception") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "fault_clear_requires_exception") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

/**
 * @brief 楠岃瘉缁熶竴瑙﹀彂鍏ュ彛鐨?homing 鍦?STOPPED 涓嬪彲鐩存帴鎴愬姛闂幆鍒?IDLE銆? */
static int verify_trigger_homing_accepted_from_stopped(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_HOMING,
        0,
        "homing",
        "trigger-homing",
        system_context_current_time_ms(system_context));
    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "homing_completed") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_HOMING);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.previous_state, "stopped") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.current_state, "idle") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "homing_completed") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

/**
 * @brief 楠岃瘉缁熶竴瑙﹀彂鍏ュ彛鐨?start 鍦?IDLE 涓嬪彲鐩存帴鎴愬姛闂幆鍒?RUNNING銆? */
static int verify_trigger_start_accepted_from_idle(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_START,
        "wash_step_control_v1",
        0,
        "trigger-start-accepted",
        system_context_current_time_ms(system_context));
    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_RUNNING);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "session_started") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_START);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.previous_state, "idle") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.current_state, "running") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "session_started") == 0);

    test_release_system_context(system_context);
    return 0;
}

/**
 * @brief 楠岃瘉缁熶竴瑙﹀彂鍏ュ彛鐨?stop 鍦?RUNNING 涓嬪彲鐩存帴鎴愬姛闂幆鍒?STOPPED銆? */
static int verify_trigger_stop_accepted_from_running(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_RUNNING);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_STOP,
        0,
        "trigger-stop-accepted",
        "trigger-stop",
        system_context_current_time_ms(system_context));
    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "trigger-stop-accepted") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_STOP);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.previous_state, "running") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.current_state, "stopped") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "trigger-stop-accepted") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    test_release_system_context(system_context);
    return 0;
}

/**
 * @brief 楠岃瘉缁熶竴瑙﹀彂鍏ュ彛鐨?fault clear 鍦?EXCEPTION 涓嬪彲鐩存帴鎴愬姛闂幆鍒?STOPPED銆? */
static int verify_trigger_fault_clear_accepted_from_exception(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_submit_fault_with_reason(system_context, "E_STOP", "trigger-clear-exception");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_FAULT,
        0,
        "clear",
        0,
        system_context_current_time_ms(system_context));
    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.previous_state, "exception") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.current_state, "stopped") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(!system_context_private_runtime(system_context)->global_fault_present);

    test_release_system_context(system_context);
    return 0;
}

static int verify_exception_requires_fault_clear_before_homing(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_submit_fault_with_reason(system_context, "E_STOP", "device-exception");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);

    result = test_process_command_and_flush(system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=device_state_exception") != 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "device_state_exception") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_START);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "device_state_exception") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=homing_requires_stopped") != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_HOMING);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "homing_requires_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    result = test_process_command_and_flush(system_context,
        "stop",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=stop_requires_running") != 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "stop_requires_running") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_STOP);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "stop_requires_running") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    result = test_process_command_and_flush(system_context,
        "fault clear",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=global_fault_cleared") != 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(!system_context_private_runtime(system_context)->global_fault_present);

    result = test_process_command_and_flush(system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "device_state_stopped") != 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "device_state_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_START);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "device_state_stopped") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "homing_completed") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_HOMING);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "homing_completed") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = test_process_command_and_flush(system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_RUNNING);

    test_release_system_context(system_context);
    return 0;
}

int main(void)
{
    if (verify_power_on_defaults_to_stopped_and_blocks_start() != 0) {
        return 1;
    }
    if (verify_homing_rejected_from_idle() != 0) {
        return 1;
    }
    if (verify_stop_rejected_from_stopped_and_idle() != 0) {
        return 1;
    }
    if (verify_fault_clear_rejected_from_stopped_and_idle() != 0) {
        return 1;
    }
    if (verify_homing_clears_alarm_before_recovering() != 0) {
        return 1;
    }
    if (verify_homing_success_runs_real_flow_and_returns_to_idle() != 0) {
        return 1;
    }
    if (verify_homing_alarm_during_recovering_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_homing_stop_all_failure_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_homing_roof_home_command_failure_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_homing_snapshot_failure_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_homing_feedback_timeout_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_homing_not_reached_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_running_manual_stop_moves_device_to_stopped() != 0) {
        return 1;
    }
    if (verify_trigger_start_rejected_from_stopped_with_matrix_reason() != 0) {
        return 1;
    }
    if (verify_trigger_start_rejected_from_exception_with_matrix_reason() != 0) {
        return 1;
    }
    if (verify_trigger_homing_rejected_from_idle_and_exception() != 0) {
        return 1;
    }
    if (verify_trigger_stop_rejected_from_stopped_and_idle() != 0) {
        return 1;
    }
    if (verify_trigger_fault_clear_rejected_from_stopped_and_idle() != 0) {
        return 1;
    }
    if (verify_trigger_homing_accepted_from_stopped() != 0) {
        return 1;
    }
    if (verify_trigger_start_accepted_from_idle() != 0) {
        return 1;
    }
    if (verify_trigger_stop_accepted_from_running() != 0) {
        return 1;
    }
    if (verify_trigger_fault_clear_accepted_from_exception() != 0) {
        return 1;
    }
    if (verify_exception_requires_fault_clear_before_homing() != 0) {
        return 1;
    }
    return 0;
}
