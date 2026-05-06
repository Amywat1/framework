#include "tests/test_support.h"

#include "domain/services/program_snapshot_service.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = program_snapshot_service_capture(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.program_snapshot.validation_result == PROGRAM_SNAPSHOT_VALIDATION_VALID);

    result = program_snapshot_service_capture(&system_context, "invalid_program");
    TEST_ASSERT(!result.ok);

    result = program_snapshot_service_capture(&system_context, "missing_program");
    TEST_ASSERT(!result.ok);
    return 0;
}
