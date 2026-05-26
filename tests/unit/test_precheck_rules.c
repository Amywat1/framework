#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

#include "domain/services/precheck_service.h"

int main(void)
{
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = precheck_service_run(device_runtime_private_runtime(system_context)->sensor_port);
    TEST_ASSERT(result.ok);

    driver_context.sensor_snapshot.vehicle_present = false;
    result = precheck_service_run(device_runtime_private_runtime(system_context)->sensor_port);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_VEHICLE_NOT_ALLOWED);
    test_release_system_context(system_context);
    return 0;
}

