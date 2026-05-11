#include <string.h>

#include "application/use_cases/process_formal_command.h"
#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_reset_preserves_bound_ports_and_occupied_state(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    const program_repository_port_t *program_repository_port_before;
    const program_repository_port_t *program_repository_port_after;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    main_loop_advance_time(system_context, 33ul);
    TEST_ASSERT(system_context_private_debug_is_in_use(system_context));
    TEST_ASSERT(system_context_private_debug_free_count() + 1u == system_context_private_debug_capacity());

    program_repository_port_before = system_context_program_repository_port(system_context);
    TEST_ASSERT(program_repository_port_before != 0);
    TEST_ASSERT(program_repository_port_before->context != 0);

    result = system_context_reset(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_debug_is_in_use(system_context));
    TEST_ASSERT(system_context_private_debug_free_count() + 1u == system_context_private_debug_capacity());
    TEST_ASSERT(system_context_current_time_ms(system_context) == 0ul);
    TEST_ASSERT(system_context_pending_trigger_count(system_context) == 0u);

    program_repository_port_after = system_context_program_repository_port(system_context);
    TEST_ASSERT(program_repository_port_after != 0);
    TEST_ASSERT(program_repository_port_after->context == program_repository_port_before->context);

    test_release_system_context(system_context);
    return 0;
}

static int verify_release_invalidates_runtime_entrypoints(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    char response_line[256];

    test_setup_system_context(&system_context, &driver_context);
    test_release_system_context(system_context);

    result = system_context_reset(system_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    memset(response_line, 0, sizeof(response_line));
    result = process_formal_command_execute(system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strstr(response_line, "result=invalid_state") != 0);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, "released", "released", 0ul);
    result = main_loop_submit_trigger(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = main_loop_run(system_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

int main(void)
{
    if (verify_reset_preserves_bound_ports_and_occupied_state() != 0) {
        return 1;
    }
    if (verify_release_invalidates_runtime_entrypoints() != 0) {
        return 1;
    }
    return 0;
}
