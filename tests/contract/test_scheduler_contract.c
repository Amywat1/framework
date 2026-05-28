#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

int main(void)
{
    scheduler_state_view_t view_from_scheduler;
    scheduler_state_view_t view_from_context;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    scheduler = test_create_scheduler( 100ul);
    TEST_ASSERT(scheduler != 0);

    result = scheduler_read_view(scheduler, &view_from_scheduler);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(view_from_scheduler.runtime_state == SCHEDULER_RUNTIME_STATE_INITIALIZED);
    TEST_ASSERT(view_from_scheduler.control_period_ms == 100ul);
    TEST_ASSERT(view_from_scheduler.command_source_state == SCHEDULER_EVENT_SOURCE_DEGRADED);
    TEST_ASSERT(view_from_scheduler.notification_source_state == SCHEDULER_EVENT_SOURCE_DEGRADED);
    TEST_ASSERT(view_from_scheduler.exit_source_state == SCHEDULER_EVENT_SOURCE_DEGRADED);

    result = test_scheduler_read_bound_view( &view_from_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(view_from_context.control_period_ms == 100ul);
    TEST_ASSERT(view_from_context.metrics.pending_trigger_count == 0u);

    TEST_ASSERT(test_scheduler_tick(scheduler, 1u) == 0);
    result = scheduler_read_view(scheduler, &view_from_scheduler);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(view_from_scheduler.runtime_state == SCHEDULER_RUNTIME_STATE_RUNNING);
    TEST_ASSERT(view_from_scheduler.metrics.cycle_count == 1ul);

    result = control_context_deinit();
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    test_release_system_context();
    return 0;
}

