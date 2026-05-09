#include "adapters/inbound/cli_command_adapter.h"
#include "application/use_cases/process_formal_command.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_formal_command_execute_is_public_entry(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    result = process_formal_command_execute(&system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "accepted=true") != 0);
    TEST_ASSERT(system_context.pending_trigger_count == 0u);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(system_context.last_reason_code[0] != '\0');
    return 0;
}

static int verify_cli_execute_uses_formal_entry(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    result = cli_command_adapter_execute_formal_line(&system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "accepted=true") != 0);
    TEST_ASSERT(system_context.pending_trigger_count == 0u);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(system_context.last_reason_code[0] != '\0');
    return 0;
}

int main(void)
{
    if (verify_formal_command_execute_is_public_entry() != 0) {
        return 1;
    }
    if (verify_cli_execute_uses_formal_entry() != 0) {
        return 1;
    }
    return 0;
}
