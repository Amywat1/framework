#include "application/coordinators/controller_runtime.h"
#include "tests/test_support.h"

static operation_result_t create_runtime_with_paths(controller_runtime_t **controller_runtime,
    simulated_driver_context_t *driver_context,
    const char *config_root,
    const char *event_log_path)
{
    controller_runtime_config_t controller_runtime_config;
    controller_scheduler_config_t controller_scheduler_config;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;

    simulated_driver_context_init(driver_context);
    memset(&sensor_port, 0, sizeof(sensor_port));
    memset(&actuator_port, 0, sizeof(actuator_port));
    simulated_sensor_driver_bind(&sensor_port, driver_context);
    simulated_gantry_driver_bind(&actuator_port, driver_context);
    simulated_brush_driver_bind(&actuator_port, driver_context);
    simulated_chemical_driver_bind(&actuator_port, driver_context);
    simulated_ro_water_driver_bind(&actuator_port, driver_context);
    simulated_dryer_driver_bind(&actuator_port, driver_context);
    test_init_scheduler_config(&controller_scheduler_config, 100ul);

    controller_runtime_config_init(&controller_runtime_config);
    controller_runtime_config.sensor_port = &sensor_port;
    controller_runtime_config.actuator_port = &actuator_port;
    controller_runtime_config.scheduler_config = &controller_scheduler_config;
    controller_runtime_config.config_root = config_root;
    controller_runtime_config.event_log_path = event_log_path;
    return controller_runtime_create(controller_runtime, &controller_runtime_config);
}

int main(void)
{
    simulated_driver_context_t driver_context;
    controller_runtime_t *controller_runtime;
    operation_result_t result;

    result = controller_runtime_destroy(0);
    TEST_ASSERT(result.ok);

    result = create_runtime_with_paths(&controller_runtime,
        &driver_context,
        "./configs-missing",
        "./runtime/logs/test_events.log");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_IO_FAILED);

    result = create_runtime_with_paths(&controller_runtime,
        &driver_context,
        "./configs",
        "./runtime/logs/test_events.log");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime != 0);

    result = controller_runtime_destroy(controller_runtime);
    TEST_ASSERT(result.ok);
    result = controller_runtime_destroy(controller_runtime);
    TEST_ASSERT(result.ok);
    return 0;
}
