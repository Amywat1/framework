#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

#include "application/use_cases/query_wash_session_status.h"

int main(void)
{
    char response_line[512];
    const program_repository_port_t *program_repository_port_before;
    const program_repository_port_t *program_repository_port_after;
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_session_status_view_t wash_session_status_view;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);

    result = test_process_command(system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "lifecycle=exiting") != 0);
    TEST_ASSERT(strstr(response_line, "stage=roof_segment") != 0);

    program_repository_port_before = system_context_program_repository_port(system_context);
    TEST_ASSERT(program_repository_port_before != 0);
    TEST_ASSERT(program_repository_port_before->context != 0);

    result = system_context_reset(system_context);
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_NONE);
    TEST_ASSERT(system_context_current_time_ms(system_context) == 0ul);
    TEST_ASSERT(system_context_pending_trigger_count(system_context) == 0u);

    program_repository_port_after = system_context_program_repository_port(system_context);
    TEST_ASSERT(program_repository_port_after != 0);
    TEST_ASSERT(program_repository_port_after->context == program_repository_port_before->context);

    simulated_driver_context_init(&driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);

    result = system_context_release(system_context);
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

