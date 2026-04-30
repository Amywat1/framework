#include <stdio.h>

#include "adapters/logging/file_event_logger.h"
#include "adapters/outbound/file_program_repository.h"
#include "platform/drivers/simulated_brush_driver.h"
#include "platform/drivers/simulated_chemical_driver.h"
#include "platform/drivers/simulated_driver_context.h"
#include "platform/drivers/simulated_gantry_driver.h"
#include "platform/drivers/simulated_sensor_driver.h"
#include "platform/linux/main_loop.h"

int main(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    simulated_driver_context_init(&driver_context);
    simulated_sensor_driver_bind(&system_context.sensor_port, &driver_context);
    simulated_gantry_driver_bind(&system_context.actuator_port, &driver_context);
    simulated_brush_driver_bind(&system_context.actuator_port, &driver_context);
    simulated_chemical_driver_bind(&system_context.actuator_port, &driver_context);
    file_program_repository_init(&system_context, "./configs");
    file_event_logger_init(&system_context, "./runtime/logs/events.log");

    result = main_loop_run(&system_context, "standard_wash");
    printf("wash_controller result=%d\n", (int)result.error_code);
    return result.ok ? 0 : 1;
}
