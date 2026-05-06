#include "tests/test_support.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;
    wash_program_t updated_program;
    char running_snapshot_id[32];

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    strncpy(running_snapshot_id, system_context.program_snapshot.program_snapshot_id, sizeof(running_snapshot_id) - 1);

    wash_program_init(&updated_program, "standard_wash", "标准洗车-新版本");
    updated_program.revision = 3;
    updated_program.stages[0] = system_context.program_snapshot.frozen_program.stages[0];
    updated_program.stage_count = 1;
    file_program_repository_set_runtime_program(&system_context, &updated_program, 3);

    TEST_ASSERT(strcmp(running_snapshot_id, system_context.program_snapshot.program_snapshot_id) == 0);

    result = test_submit_feedback(&system_context, "feedback:pre_soak", "snapshot-1");
    TEST_ASSERT(result.ok);
    result = test_submit_feedback(&system_context, "feedback:wash", "snapshot-2");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_COMPLETED);

    wash_session_reset(&system_context.wash_session);
    wash_execution_reset(&system_context.wash_execution);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.program_snapshot.source_revision == 3);
    return 0;
}
