#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

#include "domain/services/program_snapshot_service.h"

int main(void)
{
    program_snapshot_service_args_t program_snapshot_service_args;
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    program_snapshot_service_args = test_build_program_snapshot_service_args(system_context);
    result = program_snapshot_service_capture(&program_snapshot_service_args, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->program_snapshot.validation_result == PROGRAM_SNAPSHOT_VALIDATION_VALID);

    result = program_snapshot_service_capture(&program_snapshot_service_args, "invalid_program");
    TEST_ASSERT(!result.ok);

    result = program_snapshot_service_capture(&program_snapshot_service_args, "missing_program");
    TEST_ASSERT(!result.ok);
    test_release_system_context(system_context);
    return 0;
}

