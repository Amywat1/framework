#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

int main(void)
{
    controller_runtime_state_view_t view_from_scheduler;
    controller_runtime_state_view_t view_from_context;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    controller_scheduler = test_create_scheduler(system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);

    result = controller_scheduler_read_view(controller_scheduler, &view_from_scheduler);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(view_from_scheduler.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_INITIALIZED);
    TEST_ASSERT(view_from_scheduler.control_period_ms == 100ul);
    TEST_ASSERT(view_from_scheduler.command_source_state == CONTROLLER_SCHEDULER_EVENT_SOURCE_DEGRADED);
    TEST_ASSERT(view_from_scheduler.notification_source_state == CONTROLLER_SCHEDULER_EVENT_SOURCE_DEGRADED);
    TEST_ASSERT(view_from_scheduler.exit_source_state == CONTROLLER_SCHEDULER_EVENT_SOURCE_DEGRADED);

    result = controller_scheduler_read_context_view(system_context, &view_from_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(view_from_context.control_period_ms == 100ul);
    TEST_ASSERT(view_from_context.metrics.pending_trigger_count == 0u);

    TEST_ASSERT(test_scheduler_tick(controller_scheduler, 1u) == 0);
    result = controller_scheduler_read_view(controller_scheduler, &view_from_scheduler);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(view_from_scheduler.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_RUNNING);
    TEST_ASSERT(view_from_scheduler.metrics.cycle_count == 1ul);

    result = system_context_release(system_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    test_release_system_context(system_context);
    return 0;
}

