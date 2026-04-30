#ifndef TESTS_TEST_SUPPORT_H
#define TESTS_TEST_SUPPORT_H

#include <stdio.h>
#include <string.h>

#include "adapters/logging/file_event_logger.h"
#include "adapters/outbound/file_program_repository.h"
#include "application/coordinators/system_context.h"
#include "platform/drivers/simulated_brush_driver.h"
#include "platform/drivers/simulated_chemical_driver.h"
#include "platform/drivers/simulated_driver_context.h"
#include "platform/drivers/simulated_gantry_driver.h"
#include "platform/drivers/simulated_sensor_driver.h"

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "ASSERT FAILED: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

static inline void test_setup_system_context(system_context_t *system_context, simulated_driver_context_t *driver_context)
{
    memset(system_context, 0, sizeof(*system_context));
    simulated_driver_context_init(driver_context);
    simulated_sensor_driver_bind(&system_context->sensor_port, driver_context);
    simulated_gantry_driver_bind(&system_context->actuator_port, driver_context);
    simulated_brush_driver_bind(&system_context->actuator_port, driver_context);
    simulated_chemical_driver_bind(&system_context->actuator_port, driver_context);
    file_program_repository_init(system_context, "./configs");
    file_event_logger_init(system_context, "./runtime/logs/test_events.log");
}

#endif
