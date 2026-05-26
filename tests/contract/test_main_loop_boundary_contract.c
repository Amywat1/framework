#include "application/use_cases/process_formal_command.h"
#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

int main(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    device_runtime_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);

    result = process_formal_command_execute(system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "detail=queued") != 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->pending_trigger_count == 1u);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state != SESSION_STATE_RUNNING);

    control_tick_advance_time(system_context, 100ul);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->current_time_ms == 100ul);
    result = control_tick_run(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->pending_trigger_count == 0u);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);
    return 0;
}

