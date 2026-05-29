#include "domain/model/program_snapshot.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_session.h"
#include "tests/test_support.h"

static int verify_wait_and_snapshot_do_not_overwrite_other_objects(void)
{
    wash_session_t wash_session;
    wash_execution_t wash_execution;
    wait_condition_t wait_condition;
    program_snapshot_t program_snapshot;
    wash_program_t wash_program;

    memset(&wash_program, 0, sizeof(wash_program));
    wash_session_reset(&wash_session);
    wash_execution_reset(&wash_execution);
    wait_condition_reset(&wait_condition);
    program_snapshot_reset(&program_snapshot);

    wash_session_abort(&wash_session, RESULT_CODE_MANUAL_ABORT, "owned-by-session", 10ul);
    wait_condition_arm(&wait_condition,
        "exec-1",
        "segment_timeout",
        20ul,
        40ul,
        WAIT_TIMEOUT_POLICY_SEGMENT,
        1ul);
    program_snapshot_capture(&program_snapshot,
        "wash_step_control_v1",
        7,
        30ul,
        &wash_program,
        PROGRAM_SNAPSHOT_VALIDATION_VALID);

    TEST_ASSERT(wash_session.final_session_result == RESULT_CODE_MANUAL_ABORT);
    TEST_ASSERT(strcmp(wash_session.abort_reason, "owned-by-session") == 0);
    TEST_ASSERT(wash_execution.execution_state == EXECUTION_STATE_NONE);
    TEST_ASSERT(wait_condition.active);
    TEST_ASSERT(strcmp(wait_condition.reason_code, "segment_timeout") == 0);
    TEST_ASSERT(strcmp(program_snapshot.source_program_id, "wash_step_control_v1") == 0);
    TEST_ASSERT(strcmp(program_snapshot.program_snapshot_id, "snapshot-30") == 0);
    return 0;
}

static int verify_execution_does_not_claim_session_final_sink(void)
{
    wash_session_t wash_session;
    wash_execution_t wash_execution;

    wash_session_reset(&wash_session);
    wash_execution_reset(&wash_execution);
    wash_execution_start_segment(&wash_execution,
        "session-1",
        0,
        "roof_segment",
        100ul,
        1ul);
    wash_execution_complete(&wash_execution,
        EXECUTION_RESULT_SEGMENT_COMPLETED,
        EXECUTION_END_REASON_NORMAL,
        200ul);

    TEST_ASSERT(wash_execution.execution_state == EXECUTION_STATE_COMPLETED);
    TEST_ASSERT(wash_execution.execution_result == EXECUTION_RESULT_SEGMENT_COMPLETED);
    TEST_ASSERT(wash_session.final_session_result == RESULT_CODE_START_FAILED);
    TEST_ASSERT(wash_session.session_state == SESSION_STATE_NONE);
    return 0;
}

int main(void)
{
    if (verify_wait_and_snapshot_do_not_overwrite_other_objects() != 0) {
        return 1;
    }
    if (verify_execution_does_not_claim_session_final_sink() != 0) {
        return 1;
    }
    return 0;
}

