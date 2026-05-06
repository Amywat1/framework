#include "tests/test_support.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;
    wash_program_t updated_program;

    test_setup_system_context(&system_context, &driver_context);

    result = test_start_session(&system_context, "missing_program");
    TEST_ASSERT(!result.ok);

    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(!result.ok);

    updated_program = system_context.program_snapshot.frozen_program;
    updated_program.revision = 9;
    file_program_repository_set_runtime_program(&system_context, &updated_program, 9);
    TEST_ASSERT(system_context.program_snapshot.source_revision != 9);
    return 0;
}
