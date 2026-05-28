#include "tests/test_support.h"

#include "application/services/alarm_evaluator.h"
#include "src/application/coordinators/control_context_private.h"
#include "src/application/jobs/alarm_detect_job.h"

static void build_background_alarm_snapshot(sensor_snapshot_t *sensor_snapshot, bool estop_active)
{
    memset(sensor_snapshot, 0, sizeof(*sensor_snapshot));
    sensor_snapshot->safety_ok = true;
    sensor_snapshot->resource_ok = true;
    sensor_snapshot->position_ok = true;
    sensor_snapshot->vehicle_present = true;
    sensor_snapshot->vehicle_allowed = true;
    sensor_snapshot->estop_active = estop_active;
}

static int trigger_background_estop_fault(
    alarm_evaluator_t *alarm_evaluator,
    sensor_snapshot_t *sensor_snapshot,
    unsigned long occurred_at_ms)
{
    operation_result_t result;

    build_background_alarm_snapshot(sensor_snapshot, true);
    result = alarm_detect_job_process_snapshot(alarm_evaluator, sensor_snapshot, occurred_at_ms);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_external_trigger_count() == 1u);

    result = control_tick_run();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_external_trigger_count() == 0u);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(control_context_private_runtime_mutable()->global_fault_present);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->global_fault_code, "estop_active") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->global_fault_reason,
        "background-alarm-monitor") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.previous_state, "idle")
        == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.current_state,
        "exception") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.result_code, "accepted")
        == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.reason_code,
        "global_fault_recorded") == 0);
    return 0;
}

static int verify_background_alarm_monitor_fault_queue_semantics(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    unsigned long first_recorded_at_ms;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(control_context_private_external_trigger_count() == 0u);

    TEST_ASSERT(trigger_background_estop_fault(&alarm_evaluator, &sensor_snapshot, 100ul) == 0);
    first_recorded_at_ms = control_context_private_runtime_mutable()->last_transition_record.recorded_at_ms;

    result = alarm_detect_job_process_snapshot( &alarm_evaluator, &sensor_snapshot, 200ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_external_trigger_count() == 0u);
    TEST_ASSERT(control_context_private_runtime_mutable()->last_transition_record.recorded_at_ms ==
        first_recorded_at_ms);

    build_background_alarm_snapshot(&sensor_snapshot, false);
    result = alarm_detect_job_process_snapshot( &alarm_evaluator, &sensor_snapshot, 300ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_external_trigger_count() == 0u);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(control_context_private_runtime_mutable()->global_fault_present);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->last_transition_record.recorded_at_ms ==
        first_recorded_at_ms);

    build_background_alarm_snapshot(&sensor_snapshot, true);
    result = alarm_detect_job_process_snapshot( &alarm_evaluator, &sensor_snapshot, 400ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_external_trigger_count() == 1u);

    test_release_system_context();
    return 0;
}

static int verify_background_alarm_monitor_retries_after_queue_full(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    sensor_snapshot_t recovered_snapshot;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    unsigned int index;

    test_setup_system_context( &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_IDLE);

    build_background_alarm_snapshot(&sensor_snapshot, true);
    for (index = 0u; index < MAX_EXTERNAL_TRIGGER_QUEUE_COUNT; ++index)
    {
        wash_trigger_event_init(&wash_trigger_event,
            TRIGGER_TYPE_STOP,
            0,
            "queue-fill",
            "test-queue-fill",
            100ul + (unsigned long)index);
        result = control_context_private_enqueue_external_trigger( &wash_trigger_event);
        TEST_ASSERT(result.ok);
    }

    TEST_ASSERT(control_context_private_external_trigger_count() == MAX_EXTERNAL_TRIGGER_QUEUE_COUNT);

    result = alarm_detect_job_process_snapshot( &alarm_evaluator, &sensor_snapshot, 200ul);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);
    TEST_ASSERT(control_context_private_external_trigger_count() == MAX_EXTERNAL_TRIGGER_QUEUE_COUNT);

    TEST_ASSERT(control_context_private_try_pop_external_trigger( &wash_trigger_event));
    TEST_ASSERT(control_context_private_external_trigger_count() == (MAX_EXTERNAL_TRIGGER_QUEUE_COUNT - 1u));

    build_background_alarm_snapshot(&recovered_snapshot, false);
    result = alarm_detect_job_process_snapshot( &alarm_evaluator, &recovered_snapshot, 300ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_external_trigger_count() == MAX_EXTERNAL_TRIGGER_QUEUE_COUNT);
    for (index = 0u; index < (MAX_EXTERNAL_TRIGGER_QUEUE_COUNT - 1u); ++index)
    {
        TEST_ASSERT(control_context_private_try_pop_external_trigger( &wash_trigger_event));
        TEST_ASSERT(strcmp(wash_trigger_event.signal_code, "queue-fill") == 0);
    }
    TEST_ASSERT(control_context_private_try_pop_external_trigger( &wash_trigger_event));
    TEST_ASSERT(wash_trigger_event.trigger_type == TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(wash_trigger_event.signal_code, "estop_active") == 0);
    TEST_ASSERT(wash_trigger_event.occurred_at_ms == 200ul);
    TEST_ASSERT(strcmp(wash_trigger_event.source, "background-alarm-monitor") == 0);
    TEST_ASSERT(control_context_private_external_trigger_count() == 0u);

    test_release_system_context();
    return 0;
}

static int verify_background_alarm_monitor_recovery_path(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_IDLE);

    TEST_ASSERT(trigger_background_estop_fault(&alarm_evaluator, &sensor_snapshot, 100ul) == 0);

    result = test_clear_fault_and_flush();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(!control_context_private_runtime_mutable()->global_fault_present);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.previous_state,
        "exception") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.current_state,
        "stopped") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.result_code, "accepted")
        == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.reason_code,
        "global_fault_cleared") == 0);

    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(!control_context_private_runtime_mutable()->global_fault_present);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "homing_completed") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->last_transition_record.trigger_type ==
        TRIGGER_TYPE_HOMING);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.previous_state,
        "stopped") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.current_state, "idle")
        == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.result_code, "accepted")
        == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.reason_code,
        "homing_completed") == 0);

    test_release_system_context();
    return 0;
}

static int verify_background_alarm_monitor_external_fault_beats_pending_backlog(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    unsigned int pending_before;
    unsigned int index;

    test_setup_system_context( &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_IDLE);

    for (index = 0u; index < MAX_PENDING_TRIGGER_COUNT; ++index)
    {
        wash_trigger_event_init(&wash_trigger_event,
            TRIGGER_TYPE_STOP,
            0,
            "backlog-stop",
            "test-backlog-stop",
            100ul + (unsigned long)index);
        result = control_context_append_trigger( &wash_trigger_event);
        TEST_ASSERT(result.ok);
    }
    pending_before = control_context_pending_trigger_count();
    TEST_ASSERT(pending_before == MAX_PENDING_TRIGGER_COUNT);

    build_background_alarm_snapshot(&sensor_snapshot, true);
    result = alarm_detect_job_process_snapshot( &alarm_evaluator, &sensor_snapshot, 200ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_external_trigger_count() == 1u);

    result = control_tick_run();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(control_context_private_runtime_mutable()->global_fault_present);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->global_fault_code, "estop_active") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);
    TEST_ASSERT(control_context_pending_trigger_count() == pending_before);
    TEST_ASSERT(control_context_private_external_trigger_count() == 0u);

    test_release_system_context();
    return 0;
}

int main(void)
{
    if (verify_background_alarm_monitor_fault_queue_semantics() != 0) {
        return 1;
    }
    if (verify_background_alarm_monitor_retries_after_queue_full() != 0) {
        return 1;
    }
    if (verify_background_alarm_monitor_recovery_path() != 0) {
        return 1;
    }
    if (verify_background_alarm_monitor_external_fault_beats_pending_backlog() != 0) {
        return 1;
    }
    return 0;
}
