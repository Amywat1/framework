#define _POSIX_C_SOURCE 200809L

#include <time.h>

#include "platform/worker_thread.h"
#include "tests/test_support.h"

#include "application/services/alarm_evaluator.h"
#include "src/application/coordinators/system_context_private.h"
#include "src/application/jobs/alarm_detect_job.h"

typedef struct runtime_runner_context_t
{
    controller_runtime_t *controller_runtime;
    operation_result_t run_result;
} runtime_runner_context_t;

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

static operation_result_t create_runtime_with_background_alarm_monitor(controller_runtime_t **controller_runtime,
    simulated_driver_context_t *driver_context,
    unsigned long control_period_ms,
    unsigned long io_sample_period_ms,
    unsigned long detect_period_ms)
{
    controller_runtime_config_t controller_runtime_config;
    controller_scheduler_config_t controller_scheduler_config;
    test_runtime_binding_t *binding;
    operation_result_t result;

    if (controller_runtime == 0 || driver_context == 0 ||
        io_sample_period_ms == 0ul || detect_period_ms == 0ul) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    *controller_runtime = 0;
    binding = test_allocate_runtime_binding();
    if (binding == 0) {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    test_bind_simulated_ports(driver_context, &binding->sensor_port, &binding->actuator_port);
    test_init_scheduler_config(&controller_scheduler_config, control_period_ms);

    controller_runtime_config_init(&controller_runtime_config);
    controller_runtime_config.sensor_port = &binding->sensor_port;
    controller_runtime_config.actuator_port = &binding->actuator_port;
    controller_runtime_config.scheduler_config = &controller_scheduler_config;
    controller_runtime_config.config_root = "./configs";
    controller_runtime_config.event_log_path = "./runtime/logs/test_events.log";
    controller_runtime_config.background_alarm_monitor.enabled = true;
    controller_runtime_config.background_alarm_monitor.io_sample_period_ms = io_sample_period_ms;
    controller_runtime_config.background_alarm_monitor.detect_period_ms = detect_period_ms;
    result = controller_runtime_create(controller_runtime, &controller_runtime_config);
    if (!result.ok || *controller_runtime == 0) {
        memset(binding, 0, sizeof(*binding));
        return result;
    }

    result = test_runtime_friend_read_system_context(*controller_runtime, &binding->system_context);
    if (!result.ok || binding->system_context == 0) {
        (void)controller_runtime_destroy(*controller_runtime);
        *controller_runtime = 0;
        memset(binding, 0, sizeof(*binding));
        return result.ok ? operation_result_fail(ERROR_CODE_INVALID_STATE) : result;
    }

    binding->occupied = true;
    binding->controller_runtime = *controller_runtime;
    return operation_result_ok();
}

static void runtime_sleep_ms(unsigned long sleep_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = (time_t)(sleep_ms / 1000ul);
    sleep_time.tv_nsec = (long)((sleep_ms % 1000ul) * 1000000ul);
    (void)nanosleep(&sleep_time, 0);
}

static void runtime_runner_entry(worker_thread_t *worker_thread, void *context)
{
    runtime_runner_context_t *runtime_runner_context;

    (void)worker_thread;
    runtime_runner_context = (runtime_runner_context_t *)context;
    if (runtime_runner_context == 0) {
        return;
    }

    runtime_runner_context->run_result = controller_runtime_run(runtime_runner_context->controller_runtime);
}

static int trigger_background_estop_fault(system_context_t system_context,
    alarm_evaluator_t *alarm_evaluator,
    sensor_snapshot_t *sensor_snapshot,
    unsigned long occurred_at_ms)
{
    operation_result_t result;

    build_background_alarm_snapshot(sensor_snapshot, true);
    result = alarm_detect_job_process_snapshot(system_context, alarm_evaluator, sensor_snapshot, occurred_at_ms);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == 1u);

    result = main_loop_run(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == 0u);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->global_fault_code, "estop_active") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->global_fault_reason,
        "background-alarm-monitor") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.previous_state, "idle")
        == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.current_state,
        "exception") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "accepted")
        == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code,
        "global_fault_recorded") == 0);
    return 0;
}

static int verify_background_alarm_monitor_fault_queue_semantics(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    system_context_t system_context;
    unsigned long first_recorded_at_ms;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == 0u);

    TEST_ASSERT(trigger_background_estop_fault(system_context, &alarm_evaluator, &sensor_snapshot, 100ul) == 0);
    first_recorded_at_ms = system_context_private_runtime(system_context)->last_transition_record.recorded_at_ms;

    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &sensor_snapshot, 200ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == 0u);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.recorded_at_ms ==
        first_recorded_at_ms);

    build_background_alarm_snapshot(&sensor_snapshot, false);
    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &sensor_snapshot, 300ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == 0u);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.recorded_at_ms ==
        first_recorded_at_ms);

    build_background_alarm_snapshot(&sensor_snapshot, true);
    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &sensor_snapshot, 400ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == 1u);

    test_release_system_context(system_context);
    return 0;
}

static int verify_background_alarm_monitor_retries_after_queue_full(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    sensor_snapshot_t recovered_snapshot;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    unsigned int index;

    test_setup_system_context(&system_context, &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    build_background_alarm_snapshot(&sensor_snapshot, true);
    for (index = 0u; index < MAX_EXTERNAL_TRIGGER_QUEUE_COUNT; ++index)
    {
        wash_trigger_event_init(&wash_trigger_event,
            TRIGGER_TYPE_STOP,
            0,
            "queue-fill",
            "test-queue-fill",
            100ul + (unsigned long)index);
        result = system_context_private_enqueue_external_trigger(system_context, &wash_trigger_event);
        TEST_ASSERT(result.ok);
    }

    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == MAX_EXTERNAL_TRIGGER_QUEUE_COUNT);

    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &sensor_snapshot, 200ul);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == MAX_EXTERNAL_TRIGGER_QUEUE_COUNT);

    TEST_ASSERT(system_context_private_try_pop_external_trigger(system_context, &wash_trigger_event));
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == (MAX_EXTERNAL_TRIGGER_QUEUE_COUNT - 1u));

    build_background_alarm_snapshot(&recovered_snapshot, false);
    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &recovered_snapshot, 300ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == MAX_EXTERNAL_TRIGGER_QUEUE_COUNT);
    for (index = 0u; index < (MAX_EXTERNAL_TRIGGER_QUEUE_COUNT - 1u); ++index)
    {
        TEST_ASSERT(system_context_private_try_pop_external_trigger(system_context, &wash_trigger_event));
        TEST_ASSERT(strcmp(wash_trigger_event.signal_code, "queue-fill") == 0);
    }
    TEST_ASSERT(system_context_private_try_pop_external_trigger(system_context, &wash_trigger_event));
    TEST_ASSERT(wash_trigger_event.trigger_type == TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(wash_trigger_event.signal_code, "estop_active") == 0);
    TEST_ASSERT(wash_trigger_event.occurred_at_ms == 200ul);
    TEST_ASSERT(strcmp(wash_trigger_event.source, "background-alarm-monitor") == 0);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == 0u);

    test_release_system_context(system_context);
    return 0;
}

static int verify_background_alarm_monitor_recovery_path(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    TEST_ASSERT(trigger_background_estop_fault(system_context, &alarm_evaluator, &sensor_snapshot, 100ul) == 0);

    result = test_clear_fault_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(!system_context_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.previous_state,
        "exception") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.current_state,
        "stopped") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "accepted")
        == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code,
        "global_fault_cleared") == 0);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(!system_context_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "homing_completed") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type ==
        TRIGGER_TYPE_HOMING);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.previous_state,
        "stopped") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.current_state, "idle")
        == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "accepted")
        == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code,
        "homing_completed") == 0);

    test_release_system_context(system_context);
    return 0;
}

static int verify_background_alarm_monitor_external_fault_beats_pending_backlog(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    system_context_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    unsigned int pending_before;
    unsigned int index;

    test_setup_system_context(&system_context, &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    for (index = 0u; index < MAX_PENDING_TRIGGER_COUNT; ++index)
    {
        wash_trigger_event_init(&wash_trigger_event,
            TRIGGER_TYPE_STOP,
            0,
            "backlog-stop",
            "test-backlog-stop",
            100ul + (unsigned long)index);
        result = system_context_append_trigger(system_context, &wash_trigger_event);
        TEST_ASSERT(result.ok);
    }
    pending_before = system_context_pending_trigger_count(system_context);
    TEST_ASSERT(pending_before == MAX_PENDING_TRIGGER_COUNT);

    build_background_alarm_snapshot(&sensor_snapshot, true);
    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &sensor_snapshot, 200ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == 1u);

    result = main_loop_run(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->global_fault_code, "estop_active") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);
    TEST_ASSERT(system_context_pending_trigger_count(system_context) == pending_before);
    TEST_ASSERT(system_context_private_external_trigger_count(system_context) == 0u);

    test_release_system_context(system_context);
    return 0;
}

static int verify_background_alarm_monitor_runtime_double_thread_path(void)
{
    controller_runtime_t *controller_runtime;
    controller_runtime_status_view_t status_view;
    controller_scheduler_t *controller_scheduler;
    runtime_runner_context_t runtime_runner_context;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    worker_thread_config_t worker_thread_config;
    worker_thread_t *runtime_worker_thread;
    operation_result_t result;

    result = create_runtime_with_background_alarm_monitor(&controller_runtime, &driver_context, 20ul, 10ul, 10ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime != 0);

    result = test_runtime_system_context(controller_runtime, &system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context != 0);

    controller_scheduler = test_runtime_scheduler(controller_runtime);
    TEST_ASSERT(controller_scheduler != 0);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);
    driver_context.precheck_estop_active = true;

    memset(&runtime_runner_context, 0, sizeof(runtime_runner_context));
    runtime_runner_context.controller_runtime = controller_runtime;
    runtime_runner_context.run_result = operation_result_fail(ERROR_CODE_INVALID_STATE);
    memset(&worker_thread_config, 0, sizeof(worker_thread_config));
    worker_thread_config.entry = runtime_runner_entry;
    worker_thread_config.context = &runtime_runner_context;
    result = worker_thread_start(&runtime_worker_thread, &worker_thread_config);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(runtime_worker_thread != 0);

    runtime_sleep_ms(200ul);

    result = controller_scheduler_request_stop(controller_scheduler);
    TEST_ASSERT(result.ok);
    result = worker_thread_join(runtime_worker_thread);
    TEST_ASSERT(result.ok);
    worker_thread_destroy(runtime_worker_thread);

    TEST_ASSERT(runtime_runner_context.run_result.ok);
    result = controller_runtime_read_state(controller_runtime, &status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(status_view.lifecycle_state == CONTROLLER_RUNTIME_STATE_TERMINATED);
    TEST_ASSERT(status_view.run_invoked);
    TEST_ASSERT(status_view.scheduler_view_available);
    TEST_ASSERT(status_view.scheduler_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED);

    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->global_fault_code, "estop_active") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->global_fault_reason,
        "background-alarm-monitor") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);

    result = controller_runtime_destroy(controller_runtime);
    TEST_ASSERT(result.ok);
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
    if (verify_background_alarm_monitor_runtime_double_thread_path() != 0) {
        return 1;
    }
    return 0;
}
