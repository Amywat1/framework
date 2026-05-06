#include "tests/test_support.h"

#include "domain/model/program_snapshot.h"
#include "domain/model/state_transition_record.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_session.h"
#include "domain/model/wash_trigger_event.h"

int main(void)
{
    wait_condition_t wait_condition;
    wait_condition_t second_wait_condition;
    wash_execution_t wash_execution;
    wash_execution_t second_wash_execution;
    wash_session_t wash_session;
    wash_session_t second_wash_session;
    wash_trigger_event_t wash_trigger_event;
    state_transition_record_t state_transition_record;
    program_snapshot_t program_snapshot;
    wash_program_t wash_program;

    wash_program_init(&wash_program, "standard_wash", "标准洗车");
    program_snapshot_capture(&program_snapshot, "standard_wash", 1, 10, &wash_program, PROGRAM_SNAPSHOT_VALIDATION_VALID);
    TEST_ASSERT(strcmp(program_snapshot.source_program_id, "standard_wash") == 0);

    wash_session_create(&wash_session, "standard_wash", program_snapshot.program_snapshot_id, 11, 1);
    TEST_ASSERT(wash_session.session_state == SESSION_STATE_CREATED);
    wash_session_create(&second_wash_session, "standard_wash", program_snapshot.program_snapshot_id, 11, 2);
    TEST_ASSERT(strcmp(wash_session.session_id, second_wash_session.session_id) != 0);

    wash_execution_start_stage(&wash_execution, wash_session.session_id, 0, "pre_soak", 12, 1);
    TEST_ASSERT(wash_execution.execution_state == EXECUTION_STATE_RUNNING);
    TEST_ASSERT(strcmp(wash_execution.stage_id, "pre_soak") == 0);
    wash_execution_start_stage(&second_wash_execution, wash_session.session_id, 1, "wash", 12, 2);
    TEST_ASSERT(strcmp(wash_execution.execution_id, second_wash_execution.execution_id) != 0);

    wait_condition_arm(&wait_condition,
        wash_execution.execution_id,
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        "feedback:pre_soak",
        12,
        1012,
        WAIT_TIMEOUT_POLICY_FINISH_EXECUTION,
        3,
        1);
    TEST_ASSERT(wait_condition.active);
    TEST_ASSERT(wait_condition_matches(&wait_condition, TRIGGER_TYPE_DEVICE_FEEDBACK, "feedback:pre_soak"));
    wait_condition_arm(&second_wait_condition,
        second_wash_execution.execution_id,
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        "feedback:wash",
        12,
        2012,
        WAIT_TIMEOUT_POLICY_FINISH_EXECUTION,
        3,
        2);
    TEST_ASSERT(strcmp(wait_condition.wait_condition_id, second_wait_condition.wait_condition_id) != 0);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_DEVICE_FEEDBACK, 0, "feedback:pre_soak", "c1", 13);
    TEST_ASSERT(strcmp(wash_trigger_event.signal_code, "feedback:pre_soak") == 0);

    state_transition_record_init(&state_transition_record,
        TRANSITION_ENTITY_SESSION,
        wash_session.session_id,
        TRIGGER_TYPE_START,
        "created",
        "running",
        "accepted",
        "none",
        14);
    TEST_ASSERT(strcmp(state_transition_record.current_state, "running") == 0);
    return 0;
}
