#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"
#include "src/platform/linux/scheduler_linux_internal.h"

int main(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    operation_result_t result;

    test_setup_control_context( &driver_context);
    scheduler = test_create_scheduler( 100ul);
    TEST_ASSERT(scheduler != 0);

    scheduler->pending_notification_count = 1u;
    scheduler->pending_period_expirations = 1ul;
    result = scheduler_linux_test_step(scheduler);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(scheduler->notification_snapshot.captured_time_ms == 0ul);
    TEST_ASSERT(control_context_current_time_ms() == 100ul);

    scheduler->pending_notification_count = 1u;
    scheduler->pending_period_expirations = 1ul;
    scheduler->pending_exit_event = true;
    scheduler->exit_requested = true;
    result = scheduler_linux_test_step(scheduler);
    TEST_ASSERT(result.ok);
    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.metrics.exit_event_count == 1ul);
    TEST_ASSERT(app_state_view.metrics.cycle_count == 1ul);
    TEST_ASSERT(app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_STOPPED
        || app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_DRAINING);

    test_release_control_context();
    return 0;
}

