#define _POSIX_C_SOURCE 200809L

#include <time.h>

#include "platform/worker_thread.h"
#include "tests/test_support.h"

#include "application/services/alarm_evaluator.h"
#include "src/application/coordinators/device_runtime_private.h"
#include "src/application/jobs/alarm_detect_job.h"

typedef struct runtime_runner_context_t
{
    app_t *app;
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

static operation_result_t create_runtime_with_background_alarm_monitor(app_t **app,
    simulated_driver_context_t *driver_context,
    unsigned long control_period_ms,
    unsigned long io_sample_period_ms,
    unsigned long detect_period_ms)
{
    app_config_t app_config;
    scheduler_config_t scheduler_config;
    test_runtime_binding_t *binding;
    operation_result_t result;

    if (app == 0 || driver_context == 0 ||
        io_sample_period_ms == 0ul || detect_period_ms == 0ul) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    *app = 0;
    binding = test_allocate_runtime_binding();
    if (binding == 0) {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    test_bind_simulated_ports(driver_context, &binding->sensor_port, &binding->actuator_port);
    test_init_scheduler_config(&scheduler_config, control_period_ms);

    app_config_init(&app_config);
    app_config.sensor_port = &binding->sensor_port;
    app_config.actuator_port = &binding->actuator_port;
    app_config.scheduler_config = &scheduler_config;
    app_config.config_root = "./configs";
    app_config.background_alarm_monitor.enabled = true;
    app_config.background_alarm_monitor.io_sample_period_ms = io_sample_period_ms;
    app_config.background_alarm_monitor.detect_period_ms = detect_period_ms;
    result = app_create(app, &app_config);
    if (!result.ok || *app == 0) {
        memset(binding, 0, sizeof(*binding));
        return result;
    }

    result = test_runtime_friend_read_device_runtime(*app, &binding->device_runtime);
    if (!result.ok || binding->device_runtime == 0) {
        (void)app_destroy(*app);
        *app = 0;
        memset(binding, 0, sizeof(*binding));
        return result.ok ? operation_result_fail(ERROR_CODE_INVALID_STATE) : result;
    }

    binding->occupied = true;
    binding->app = *app;
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

    runtime_runner_context->run_result = app_run(runtime_runner_context->app);
}

static int trigger_background_estop_fault(device_runtime_t system_context,
    alarm_evaluator_t *alarm_evaluator,
    sensor_snapshot_t *sensor_snapshot,
    unsigned long occurred_at_ms)
{
    operation_result_t result;

    build_background_alarm_snapshot(sensor_snapshot, true);
    result = alarm_detect_job_process_snapshot(system_context, alarm_evaluator, sensor_snapshot, occurred_at_ms);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == 1u);

    result = control_tick_run(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == 0u);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->global_fault_code, "estop_active") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->global_fault_reason,
        "background-alarm-monitor") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.previous_state, "idle")
        == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.current_state,
        "exception") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.result_code, "accepted")
        == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.reason_code,
        "global_fault_recorded") == 0);
    return 0;
}

static int verify_background_alarm_monitor_fault_queue_semantics(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    device_runtime_t system_context;
    unsigned long first_recorded_at_ms;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == 0u);

    TEST_ASSERT(trigger_background_estop_fault(system_context, &alarm_evaluator, &sensor_snapshot, 100ul) == 0);
    first_recorded_at_ms = device_runtime_private_runtime(system_context)->last_transition_record.recorded_at_ms;

    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &sensor_snapshot, 200ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == 0u);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.recorded_at_ms ==
        first_recorded_at_ms);

    build_background_alarm_snapshot(&sensor_snapshot, false);
    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &sensor_snapshot, 300ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == 0u);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.recorded_at_ms ==
        first_recorded_at_ms);

    build_background_alarm_snapshot(&sensor_snapshot, true);
    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &sensor_snapshot, 400ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == 1u);

    test_release_system_context(system_context);
    return 0;
}

static int verify_background_alarm_monitor_retries_after_queue_full(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    sensor_snapshot_t recovered_snapshot;
    device_runtime_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    unsigned int index;

    test_setup_system_context(&system_context, &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    build_background_alarm_snapshot(&sensor_snapshot, true);
    for (index = 0u; index < MAX_EXTERNAL_TRIGGER_QUEUE_COUNT; ++index)
    {
        wash_trigger_event_init(&wash_trigger_event,
            TRIGGER_TYPE_STOP,
            0,
            "queue-fill",
            "test-queue-fill",
            100ul + (unsigned long)index);
        result = device_runtime_private_enqueue_external_trigger(system_context, &wash_trigger_event);
        TEST_ASSERT(result.ok);
    }

    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == MAX_EXTERNAL_TRIGGER_QUEUE_COUNT);

    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &sensor_snapshot, 200ul);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == MAX_EXTERNAL_TRIGGER_QUEUE_COUNT);

    TEST_ASSERT(device_runtime_private_try_pop_external_trigger(system_context, &wash_trigger_event));
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == (MAX_EXTERNAL_TRIGGER_QUEUE_COUNT - 1u));

    build_background_alarm_snapshot(&recovered_snapshot, false);
    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &recovered_snapshot, 300ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == MAX_EXTERNAL_TRIGGER_QUEUE_COUNT);
    for (index = 0u; index < (MAX_EXTERNAL_TRIGGER_QUEUE_COUNT - 1u); ++index)
    {
        TEST_ASSERT(device_runtime_private_try_pop_external_trigger(system_context, &wash_trigger_event));
        TEST_ASSERT(strcmp(wash_trigger_event.signal_code, "queue-fill") == 0);
    }
    TEST_ASSERT(device_runtime_private_try_pop_external_trigger(system_context, &wash_trigger_event));
    TEST_ASSERT(wash_trigger_event.trigger_type == TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(wash_trigger_event.signal_code, "estop_active") == 0);
    TEST_ASSERT(wash_trigger_event.occurred_at_ms == 200ul);
    TEST_ASSERT(strcmp(wash_trigger_event.source, "background-alarm-monitor") == 0);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == 0u);

    test_release_system_context(system_context);
    return 0;
}

static int verify_background_alarm_monitor_recovery_path(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    device_runtime_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    TEST_ASSERT(trigger_background_estop_fault(system_context, &alarm_evaluator, &sensor_snapshot, 100ul) == 0);

    result = test_clear_fault_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(!device_runtime_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.previous_state,
        "exception") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.current_state,
        "stopped") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.result_code, "accepted")
        == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.reason_code,
        "global_fault_cleared") == 0);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(!device_runtime_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "homing_completed") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.trigger_type ==
        TRIGGER_TYPE_HOMING);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.previous_state,
        "stopped") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.current_state, "idle")
        == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.result_code, "accepted")
        == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.reason_code,
        "homing_completed") == 0);

    test_release_system_context(system_context);
    return 0;
}

static int verify_background_alarm_monitor_external_fault_beats_pending_backlog(void)
{
    alarm_evaluator_t alarm_evaluator;
    simulated_driver_context_t driver_context;
    sensor_snapshot_t sensor_snapshot;
    device_runtime_t system_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    unsigned int pending_before;
    unsigned int index;

    test_setup_system_context(&system_context, &driver_context);
    alarm_evaluator_init(&alarm_evaluator);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    for (index = 0u; index < MAX_PENDING_TRIGGER_COUNT; ++index)
    {
        wash_trigger_event_init(&wash_trigger_event,
            TRIGGER_TYPE_STOP,
            0,
            "backlog-stop",
            "test-backlog-stop",
            100ul + (unsigned long)index);
        result = device_runtime_append_trigger(system_context, &wash_trigger_event);
        TEST_ASSERT(result.ok);
    }
    pending_before = device_runtime_pending_trigger_count(system_context);
    TEST_ASSERT(pending_before == MAX_PENDING_TRIGGER_COUNT);

    build_background_alarm_snapshot(&sensor_snapshot, true);
    result = alarm_detect_job_process_snapshot(system_context, &alarm_evaluator, &sensor_snapshot, 200ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == 1u);

    result = control_tick_run(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->global_fault_code, "estop_active") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);
    TEST_ASSERT(device_runtime_pending_trigger_count(system_context) == pending_before);
    TEST_ASSERT(device_runtime_private_external_trigger_count(system_context) == 0u);

    test_release_system_context(system_context);
    return 0;
}

static int verify_background_alarm_monitor_runtime_double_thread_path(void)
{
    app_t *app;
    app_status_view_t status_view;
    scheduler_t *scheduler;
    runtime_runner_context_t runtime_runner_context;
    simulated_driver_context_t driver_context;
    device_runtime_t system_context;
    worker_thread_config_t worker_thread_config;
    worker_thread_t *runtime_worker_thread;
    operation_result_t result;

    result = create_runtime_with_background_alarm_monitor(&app, &driver_context, 20ul, 10ul, 10ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app != 0);

    result = test_runtime_system_context(app, &system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context != 0);

    scheduler = test_runtime_scheduler(app);
    TEST_ASSERT(scheduler != 0);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);
    driver_context.precheck_estop_active = true;

    memset(&runtime_runner_context, 0, sizeof(runtime_runner_context));
    runtime_runner_context.app = app;
    runtime_runner_context.run_result = operation_result_fail(ERROR_CODE_INVALID_STATE);
    memset(&worker_thread_config, 0, sizeof(worker_thread_config));
    worker_thread_config.entry = runtime_runner_entry;
    worker_thread_config.context = &runtime_runner_context;
    result = worker_thread_start(&runtime_worker_thread, &worker_thread_config);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(runtime_worker_thread != 0);

    runtime_sleep_ms(200ul);

    result = scheduler_request_stop(scheduler);
    TEST_ASSERT(result.ok);
    result = worker_thread_join(runtime_worker_thread);
    TEST_ASSERT(result.ok);
    worker_thread_destroy(runtime_worker_thread);

    TEST_ASSERT(runtime_runner_context.run_result.ok);
    result = app_read_state(app, &status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(status_view.state == APP_STATE_TERMINATED);
    TEST_ASSERT(status_view.state == APP_STATE_TERMINATED);
    TEST_ASSERT(status_view.scheduler_view_available);
    TEST_ASSERT(status_view.scheduler_view.runtime_state == SCHEDULER_RUNTIME_STATE_STOPPED);

    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->global_fault_code, "estop_active") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->global_fault_reason,
        "background-alarm-monitor") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.trigger_type ==
        TRIGGER_TYPE_FAULT);

    result = app_destroy(app);
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
