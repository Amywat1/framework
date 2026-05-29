#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

typedef struct readonly_snapshot_t {
    bool global_fault_present;
    unsigned int pending_trigger_count;
    unsigned long current_time_ms;
    session_state_t session_state;
    execution_state_t execution_state;
    result_code_t final_session_result;
    char last_result_code[32];
    char last_reason_code[64];
    char global_fault_reason[128];
} readonly_snapshot_t;

static void capture_snapshot(readonly_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->global_fault_present = control_context_private_global_fault_present();
    snapshot->pending_trigger_count = control_context_pending_trigger_count();
    snapshot->current_time_ms = control_context_current_time_ms();
    snapshot->session_state = control_context_private_wash_session()->session_state;
    snapshot->execution_state = control_context_private_wash_execution()->execution_state;
    snapshot->final_session_result = control_context_private_wash_session()->final_session_result;
    strncpy(snapshot->last_result_code, control_context_last_result_code(), sizeof(snapshot->last_result_code) - 1);
    strncpy(snapshot->last_reason_code, control_context_last_reason_code(), sizeof(snapshot->last_reason_code) - 1);
    strncpy(snapshot->global_fault_reason, control_context_private_global_fault_reason(), sizeof(snapshot->global_fault_reason) - 1);
}

static int assert_snapshot_equal(const readonly_snapshot_t *left, const readonly_snapshot_t *right)
{
    TEST_ASSERT(left->global_fault_present == right->global_fault_present);
    TEST_ASSERT(left->pending_trigger_count == right->pending_trigger_count);
    TEST_ASSERT(left->current_time_ms == right->current_time_ms);
    TEST_ASSERT(left->session_state == right->session_state);
    TEST_ASSERT(left->execution_state == right->execution_state);
    TEST_ASSERT(left->final_session_result == right->final_session_result);
    TEST_ASSERT(strcmp(left->last_result_code, right->last_result_code) == 0);
    TEST_ASSERT(strcmp(left->last_reason_code, right->last_reason_code) == 0);
    TEST_ASSERT(strcmp(left->global_fault_reason, right->global_fault_reason) == 0);
    return 0;
}

static int verify_status_query_is_readonly(void)
{
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    readonly_snapshot_t before_snapshot;
    readonly_snapshot_t after_snapshot;
    operation_result_t result;

    test_setup_control_context( &driver_context);
    result = test_load_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    capture_snapshot(&before_snapshot);
    result = query_wash_session_status( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    result = query_wash_session_status( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    capture_snapshot(&after_snapshot);

    TEST_ASSERT(assert_snapshot_equal(&before_snapshot, &after_snapshot) == 0);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_RUNNING);
    test_release_control_context();
    return 0;
}

int main(void)
{
    return verify_status_query_is_readonly();
}

