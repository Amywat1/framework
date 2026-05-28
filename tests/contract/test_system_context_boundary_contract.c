#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

static int verify_context_holds_shared_runtime_facts(void)
{
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;
    unsigned int pending_trigger_count_before;
    unsigned long current_time_before;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    pending_trigger_count_before = control_context_private_runtime_mutable()->pending_trigger_count;
    current_time_before = control_context_private_runtime_mutable()->current_time_ms;
    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.has_active_session);
    TEST_ASSERT(control_context_private_runtime_mutable()->pending_trigger_count == pending_trigger_count_before);
    TEST_ASSERT(control_context_private_runtime_mutable()->current_time_ms == current_time_before);
    TEST_ASSERT(control_context_private_runtime_mutable()->last_result_code[0] != '\0');
    TEST_ASSERT(control_context_private_runtime_mutable()->last_reason_code[0] != '\0');
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_RUNNING);
    test_release_system_context();
    return 0;
}

static int verify_public_lifecycle_preserves_bound_ports(void)
{
    simulated_driver_context_t driver_context;
    const program_repository_port_t *program_repository_port_ptr;
    program_repository_port_t program_repository_port_before;
    program_repository_port_t program_repository_port_after;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    program_repository_port_ptr = control_context_program_repository_port();
    TEST_ASSERT(program_repository_port_ptr != 0);
    program_repository_port_before = *program_repository_port_ptr;
    TEST_ASSERT(program_repository_port_before.context != 0);

    result = control_context_reset();
    TEST_ASSERT(result.ok);
    program_repository_port_ptr = control_context_program_repository_port();
    TEST_ASSERT(program_repository_port_ptr != 0);
    program_repository_port_after = *program_repository_port_ptr;
    TEST_ASSERT(program_repository_port_after.context == program_repository_port_before.context);
    TEST_ASSERT(control_context_pending_trigger_count() == 0u);
    TEST_ASSERT(control_context_current_time_ms() == 0ul);

    test_release_system_context();
    return 0;
}

static int verify_release_clears_single_instance_and_invalidates_handle(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);

    test_release_system_context();
    TEST_ASSERT(control_context_pending_trigger_count() == 0u);
    TEST_ASSERT(control_context_current_time_ms() == 0ul);
    TEST_ASSERT(control_context_program_repository_port() == 0);

    result = control_context_deinit();
    TEST_ASSERT(!result.ok);
    return 0;
}

int main(void)
{
    if (verify_context_holds_shared_runtime_facts() != 0) {
        return 1;
    }
    if (verify_public_lifecycle_preserves_bound_ports() != 0) {
        return 1;
    }
    if (verify_release_clears_single_instance_and_invalidates_handle() != 0) {
        return 1;
    }
    return 0;
}

