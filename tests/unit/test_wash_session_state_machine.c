#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

#include "domain/services/program_snapshot_service.h"

static int verify_start_returns_transition_fact(void)
{
    program_snapshot_service_args_t program_snapshot_service_args;
    control_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    test_setup_system_context(&system_context, &driver_context);
    control_context_private_runtime_mutable()->current_time_ms = 100;
    program_snapshot_service_args = test_build_program_snapshot_service_args(system_context);
    result = program_snapshot_service_capture(&program_snapshot_service_args, "standard_wash");
    TEST_ASSERT(result.ok);

    wash_session_service_args = test_build_wash_session_service_args(system_context);
    result = wash_session_state_machine_start(&wash_session_service_args,
        "standard_wash",
        &wash_session_transition_fact);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(strcmp(wash_session_transition_fact.previous_state, "none") == 0);
    TEST_ASSERT(strcmp(wash_session_transition_fact.current_state, "running") == 0);
    TEST_ASSERT(strcmp(wash_session_transition_fact.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(wash_session_transition_fact.reason_code, "none") == 0);
    test_release_system_context(system_context);
    return 0;
}

static int verify_complete_returns_transition_fact(void)
{
    program_snapshot_service_args_t program_snapshot_service_args;
    control_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    test_setup_system_context(&system_context, &driver_context);
    program_snapshot_service_args = test_build_program_snapshot_service_args(system_context);
    result = program_snapshot_service_capture(&program_snapshot_service_args, "standard_wash");
    TEST_ASSERT(result.ok);

    wash_session_service_args = test_build_wash_session_service_args(system_context);
    result = wash_session_state_machine_start(&wash_session_service_args,
        "standard_wash",
        &wash_session_transition_fact);
    TEST_ASSERT(result.ok);

    wash_session_service_args.current_time_ms = 200;
    result = wash_session_state_machine_complete(&wash_session_service_args,
        RESULT_CODE_SUCCESS,
        &wash_session_transition_fact);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_COMPLETED);
    TEST_ASSERT(strcmp(wash_session_transition_fact.previous_state, "running") == 0);
    TEST_ASSERT(strcmp(wash_session_transition_fact.current_state, "completed") == 0);
    TEST_ASSERT(strcmp(wash_session_transition_fact.result_code, "completed") == 0);
    test_release_system_context(system_context);
    return 0;
}

static int verify_abort_returns_transition_fact(void)
{
    program_snapshot_service_args_t program_snapshot_service_args;
    control_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    test_setup_system_context(&system_context, &driver_context);
    program_snapshot_service_args = test_build_program_snapshot_service_args(system_context);
    result = program_snapshot_service_capture(&program_snapshot_service_args, "standard_wash");
    TEST_ASSERT(result.ok);

    wash_session_service_args = test_build_wash_session_service_args(system_context);
    result = wash_session_state_machine_start(&wash_session_service_args,
        "standard_wash",
        &wash_session_transition_fact);
    TEST_ASSERT(result.ok);

    wash_session_service_args.current_time_ms = 300;
    result = wash_session_state_machine_abort(&wash_session_service_args,
        RESULT_CODE_MANUAL_ABORT,
        "manual-stop",
        &wash_session_transition_fact);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(wash_session_transition_fact.previous_state, "running") == 0);
    TEST_ASSERT(strcmp(wash_session_transition_fact.current_state, "aborted") == 0);
    TEST_ASSERT(strcmp(wash_session_transition_fact.reason_code, "manual-stop") == 0);
    test_release_system_context(system_context);
    return 0;
}

int main(void)
{
    if (verify_start_returns_transition_fact() != 0) {
        return 1;
    }
    if (verify_complete_returns_transition_fact() != 0) {
        return 1;
    }
    if (verify_abort_returns_transition_fact() != 0) {
        return 1;
    }
    return 0;
}

