#include "application/coordinators/controller_runtime.h"
#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"

int main(void)
{
    controller_runtime_status_view_t status_view;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    controller_runtime_t *controller_runtime;
    system_context_t system_context;
    operation_result_t result;
    char response_line[512];

    result = test_create_runtime(&controller_runtime, &driver_context, 100ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime != 0);

    result = test_runtime_system_context(controller_runtime, &system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context != 0);
    TEST_ASSERT(test_runtime_scheduler(controller_runtime) != 0);

    result = controller_runtime_read_state(controller_runtime, &status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(status_view.lifecycle_state == CONTROLLER_RUNTIME_STATE_CREATED);
    TEST_ASSERT(status_view.system_context_acquired);
    TEST_ASSERT(status_view.scheduler_created);
    TEST_ASSERT(!status_view.run_invoked);

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_NONE);

    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    result = controller_scheduler_linux_test_inject_command(test_runtime_scheduler(controller_runtime),
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "accepted=true") != 0);

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_RUNNING);

    result = controller_runtime_destroy(controller_runtime);
    TEST_ASSERT(result.ok);

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}
