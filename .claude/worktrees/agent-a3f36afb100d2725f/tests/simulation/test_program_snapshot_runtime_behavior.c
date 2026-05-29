#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;
    wash_program_t updated_program;

    test_setup_system_context(&system_context, &driver_context);

    result = test_start_session_and_flush(system_context, "missing_program");
    TEST_ASSERT(!result.ok);

    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(!result.ok);

    updated_program = system_context_private_runtime(system_context)->program_snapshot.frozen_program;
    updated_program.revision = 9;
    file_program_repository_set_runtime_program(system_context, &updated_program, 9);
    TEST_ASSERT(system_context_private_runtime(system_context)->program_snapshot.source_revision != 9);
    test_release_system_context(system_context);
    return 0;
}

