#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"
#include "src/platform/linux/controller_scheduler_linux_internal.h"

int main(void)
{
    controller_runtime_state_view_t controller_runtime_state_view;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    controller_scheduler = test_create_scheduler(system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);

    controller_scheduler->pending_notification_count = 1u;
    controller_scheduler->pending_period_expirations = 1ul;
    result = controller_scheduler_linux_test_step(controller_scheduler);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_scheduler->notification_snapshot.captured_time_ms == 0ul);
    TEST_ASSERT(system_context_current_time_ms(system_context) == 100ul);

    controller_scheduler->pending_notification_count = 1u;
    controller_scheduler->pending_period_expirations = 1ul;
    controller_scheduler->pending_exit_event = true;
    controller_scheduler->exit_requested = true;
    result = controller_scheduler_linux_test_step(controller_scheduler);
    TEST_ASSERT(result.ok);
    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.metrics.exit_event_count == 1ul);
    TEST_ASSERT(controller_runtime_state_view.metrics.cycle_count == 1ul);
    TEST_ASSERT(controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED
        || controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_DRAINING);

    controller_scheduler_linux_destroy(controller_scheduler);
    test_release_system_context(system_context);
    return 0;
}

