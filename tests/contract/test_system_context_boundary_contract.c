#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_context_holds_shared_runtime_facts(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;
    unsigned int pending_trigger_count_before;
    unsigned long current_time_before;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    pending_trigger_count_before = system_context_private_runtime(system_context)->pending_trigger_count;
    current_time_before = system_context_private_runtime(system_context)->current_time_ms;
    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.has_active_session);
    TEST_ASSERT(system_context_private_runtime(system_context)->pending_trigger_count == pending_trigger_count_before);
    TEST_ASSERT(system_context_private_runtime(system_context)->current_time_ms == current_time_before);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_result_code[0] != '\0');
    TEST_ASSERT(system_context_private_runtime(system_context)->last_reason_code[0] != '\0');
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);
    test_release_system_context(system_context);
    return 0;
}

static int verify_public_lifecycle_preserves_bound_ports(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    const program_repository_port_t *program_repository_port_ptr;
    program_repository_port_t program_repository_port_before;
    program_repository_port_t program_repository_port_after;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    program_repository_port_ptr = system_context_program_repository_port(system_context);
    TEST_ASSERT(program_repository_port_ptr != 0);
    program_repository_port_before = *program_repository_port_ptr;
    TEST_ASSERT(program_repository_port_before.context != 0);

    result = system_context_reset(system_context);
    TEST_ASSERT(result.ok);
    program_repository_port_ptr = system_context_program_repository_port(system_context);
    TEST_ASSERT(program_repository_port_ptr != 0);
    program_repository_port_after = *program_repository_port_ptr;
    TEST_ASSERT(program_repository_port_after.context == program_repository_port_before.context);
    TEST_ASSERT(system_context_pending_trigger_count(system_context) == 0u);
    TEST_ASSERT(system_context_current_time_ms(system_context) == 0ul);

    test_release_system_context(system_context);
    return 0;
}

static int verify_release_returns_resource_to_pool_and_invalidates_handle(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    TEST_ASSERT(system_context_private_debug_is_in_use(system_context));

    result = system_context_release(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!system_context_private_debug_is_in_use(system_context));
    TEST_ASSERT(system_context_pending_trigger_count(system_context) == 0u);
    TEST_ASSERT(system_context_current_time_ms(system_context) == 0ul);
    TEST_ASSERT(system_context_program_repository_port(system_context) == 0);

    result = system_context_release(system_context);
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
    if (verify_release_returns_resource_to_pool_and_invalidates_handle() != 0) {
        return 1;
    }
    return 0;
}

