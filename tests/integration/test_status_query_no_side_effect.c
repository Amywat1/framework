#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

#include "application/use_cases/query_wash_session_status.h"

typedef struct status_side_effect_snapshot_t {
    bool global_fault_present;
    unsigned int pending_trigger_count;
    session_state_t session_state;
    result_code_t final_session_result;
    char last_result_code[32];
    char last_reason_code[64];
} status_side_effect_snapshot_t;

static void capture_status_snapshot(const system_context_t system_context, status_side_effect_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->global_fault_present = system_context_private_runtime(system_context)->global_fault_present;
    snapshot->pending_trigger_count = system_context_private_runtime(system_context)->pending_trigger_count;
    snapshot->session_state = system_context_private_runtime(system_context)->wash_session.session_state;
    snapshot->final_session_result = system_context_private_runtime(system_context)->wash_session.final_session_result;
    strncpy(snapshot->last_result_code, system_context_private_runtime(system_context)->last_result_code, sizeof(snapshot->last_result_code) - 1);
    strncpy(snapshot->last_reason_code, system_context_private_runtime(system_context)->last_reason_code, sizeof(snapshot->last_reason_code) - 1);
}

static int assert_status_snapshot_equal(const status_side_effect_snapshot_t *left,
    const status_side_effect_snapshot_t *right)
{
    TEST_ASSERT(left->global_fault_present == right->global_fault_present);
    TEST_ASSERT(left->pending_trigger_count == right->pending_trigger_count);
    TEST_ASSERT(left->session_state == right->session_state);
    TEST_ASSERT(left->final_session_result == right->final_session_result);
    TEST_ASSERT(strcmp(left->last_result_code, right->last_result_code) == 0);
    TEST_ASSERT(strcmp(left->last_reason_code, right->last_reason_code) == 0);
    return 0;
}

static int verify_status_command_has_no_side_effect(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    status_side_effect_snapshot_t before_snapshot;
    status_side_effect_snapshot_t after_snapshot;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    capture_status_snapshot(system_context, &before_snapshot);
    result = test_process_command(system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=status accepted=true") != 0);
    result = test_process_command(system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    capture_status_snapshot(system_context, &after_snapshot);

    TEST_ASSERT(assert_status_snapshot_equal(&before_snapshot, &after_snapshot) == 0);

    result = system_context_reset(system_context);
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(system_context, &(wash_session_status_view_t){0});
    TEST_ASSERT(result.ok);
    capture_status_snapshot(system_context, &after_snapshot);
    TEST_ASSERT(after_snapshot.global_fault_present == false);
    TEST_ASSERT(after_snapshot.pending_trigger_count == 0u);
    TEST_ASSERT(after_snapshot.session_state == SESSION_STATE_NONE);
    TEST_ASSERT(after_snapshot.last_result_code[0] == '\0');
    TEST_ASSERT(after_snapshot.last_reason_code[0] == '\0');

    test_release_system_context(system_context);
    result = query_wash_session_status_execute(system_context, &(wash_session_status_view_t){0});
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

int main(void)
{
    return verify_status_command_has_no_side_effect();
}

