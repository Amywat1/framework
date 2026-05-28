#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

int main(void)
{
    device_runtime_t system_context;
    device_runtime_t invalid_context;
    simulated_driver_context_t driver_context;
    simulated_driver_context_t invalid_driver_context;
    operation_result_t result;
    char snapshot_id[32];

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    strncpy(snapshot_id, device_runtime_private_runtime_mutable()->program_snapshot.program_snapshot_id, sizeof(snapshot_id) - 1);
    snapshot_id[sizeof(snapshot_id) - 1] = '\0';

    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(!result.ok);

    result = device_runtime_reset(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime_mutable()->program_snapshot.program_snapshot_id[0] == '\0');
    TEST_ASSERT(device_runtime_private_runtime_mutable()->program_snapshot.validation_result
        == PROGRAM_SNAPSHOT_VALIDATION_UNAVAILABLE);

    test_setup_system_context(&invalid_context, &invalid_driver_context);
    result = test_start_session_and_flush(invalid_context, "invalid_program");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(device_runtime_private_runtime_mutable()->program_snapshot.program_snapshot_id[0] == '\0');

    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(snapshot_id, device_runtime_private_runtime_mutable()->program_snapshot.program_snapshot_id) == 0);

    test_release_system_context(invalid_context);
    test_release_system_context(system_context);
    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

