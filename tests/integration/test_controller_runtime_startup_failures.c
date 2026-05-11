#include "application/coordinators/controller_runtime.h"
#include "tests/test_support.h"

static void build_ports(simulated_driver_context_t *driver_context,
    sensor_port_t *sensor_port,
    actuator_port_t *actuator_port)
{
    simulated_driver_context_init(driver_context);
    memset(sensor_port, 0, sizeof(*sensor_port));
    memset(actuator_port, 0, sizeof(*actuator_port));
    simulated_sensor_driver_bind(sensor_port, driver_context);
    simulated_gantry_driver_bind(actuator_port, driver_context);
    simulated_brush_driver_bind(actuator_port, driver_context);
    simulated_chemical_driver_bind(actuator_port, driver_context);
    simulated_ro_water_driver_bind(actuator_port, driver_context);
    simulated_dryer_driver_bind(actuator_port, driver_context);
}

static int verify_missing_bindings_fail_explicitly(void)
{
    controller_runtime_config_t controller_runtime_config;
    controller_scheduler_config_t controller_scheduler_config;
    controller_runtime_t *controller_runtime;
    operation_result_t result;

    test_init_scheduler_config(&controller_scheduler_config, 100ul);
    controller_runtime_config_init(&controller_runtime_config);
    controller_runtime_config.scheduler_config = &controller_scheduler_config;
    controller_runtime_config.config_root = "./configs";
    controller_runtime_config.event_log_path = "./runtime/logs/test_events.log";

    result = controller_runtime_create(&controller_runtime, &controller_runtime_config);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);
    return 0;
}

static int verify_bad_paths_fail_and_allow_immediate_recreate(void)
{
    controller_runtime_config_t controller_runtime_config;
    controller_scheduler_config_t controller_scheduler_config;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    simulated_driver_context_t driver_context;
    controller_runtime_t *controller_runtime;
    operation_result_t result;

    build_ports(&driver_context, &sensor_port, &actuator_port);
    test_init_scheduler_config(&controller_scheduler_config, 100ul);
    controller_runtime_config_init(&controller_runtime_config);
    controller_runtime_config.sensor_port = &sensor_port;
    controller_runtime_config.actuator_port = &actuator_port;
    controller_runtime_config.scheduler_config = &controller_scheduler_config;
    controller_runtime_config.config_root = "./configs-missing";
    controller_runtime_config.event_log_path = "./runtime/logs/test_events.log";

    result = controller_runtime_create(&controller_runtime, &controller_runtime_config);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_IO_FAILED);

    controller_runtime_config.config_root = "./configs";
    controller_runtime_config.event_log_path = "./runtime/logs/missing-dir/test_events.log";
    result = controller_runtime_create(&controller_runtime, &controller_runtime_config);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_IO_FAILED);

    controller_runtime_config.event_log_path = "./runtime/logs/test_events.log";
    result = controller_runtime_create(&controller_runtime, &controller_runtime_config);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime != 0);
    result = controller_runtime_destroy(controller_runtime);
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_bad_scheduler_config_fails_explicitly(void)
{
    controller_runtime_config_t controller_runtime_config;
    controller_scheduler_config_t controller_scheduler_config;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    simulated_driver_context_t driver_context;
    controller_runtime_t *controller_runtime;
    operation_result_t result;

    build_ports(&driver_context, &sensor_port, &actuator_port);
    test_init_scheduler_config(&controller_scheduler_config, 100ul);
    controller_scheduler_config.control_period_ms = 0ul;

    controller_runtime_config_init(&controller_runtime_config);
    controller_runtime_config.sensor_port = &sensor_port;
    controller_runtime_config.actuator_port = &actuator_port;
    controller_runtime_config.scheduler_config = &controller_scheduler_config;
    controller_runtime_config.config_root = "./configs";
    controller_runtime_config.event_log_path = "./runtime/logs/test_events.log";

    result = controller_runtime_create(&controller_runtime, &controller_runtime_config);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);
    return 0;
}

int main(void)
{
    if (verify_missing_bindings_fail_explicitly() != 0) {
        return 1;
    }
    if (verify_bad_paths_fail_and_allow_immediate_recreate() != 0) {
        return 1;
    }
    if (verify_bad_scheduler_config_fails_explicitly() != 0) {
        return 1;
    }
    return 0;
}
