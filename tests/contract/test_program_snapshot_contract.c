#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

int main(void)
{
    system_context_t system_context;
    system_context_t invalid_context;
    simulated_driver_context_t driver_context;
    simulated_driver_context_t invalid_driver_context;
    operation_result_t result;
    char snapshot_id[32];

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session_and_flush(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    strncpy(snapshot_id, system_context.program_snapshot.program_snapshot_id, sizeof(snapshot_id) - 1);

    result = test_start_session_and_flush(&system_context, "standard_wash");
    TEST_ASSERT(!result.ok);

    test_setup_system_context(&invalid_context, &invalid_driver_context);
    result = test_start_session_and_flush(&invalid_context, "invalid_program");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strcmp(snapshot_id, system_context.program_snapshot.program_snapshot_id) == 0);
    return 0;
}
