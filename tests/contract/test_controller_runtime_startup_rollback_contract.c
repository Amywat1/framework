#include "startup/app_bootstrap.h"
#include "tests/test_support.h"

static operation_result_t create_runtime_with_paths(app_t **app,
    simulated_driver_context_t *driver_context,
    const char *config_root)
{
    app_config_t app_config;
    scheduler_config_t scheduler_config;
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
    test_init_scheduler_config(&scheduler_config, 100ul);

    app_config_init(&app_config);
    app_config.sensor_port = &sensor_port;
    app_config.actuator_port = &actuator_port;
    app_config.scheduler_config = &scheduler_config;
    app_config.config_root = config_root;
    return app_create(app, &app_config);
}

int main(void)
{
    simulated_driver_context_t driver_context;
    app_t *app;
    operation_result_t result;

    result = app_destroy(0);
    TEST_ASSERT(result.ok);

    result = create_runtime_with_paths(&app,
        &driver_context,
        "./configs-missing");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_IO_FAILED);

    result = create_runtime_with_paths(&app,
        &driver_context,
        "./configs");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app != 0);

    result = app_destroy(app);
    TEST_ASSERT(result.ok);
    result = app_destroy(app);
    TEST_ASSERT(result.ok);
    return 0;
}
