#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

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

    controller_scheduler_linux_test_set_cycle_duration(controller_scheduler, 250ul);
    result = controller_scheduler_linux_test_inject_period(controller_scheduler, 3u);
    TEST_ASSERT(result.ok);
    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.metrics.cycle_count == 1ul);
    TEST_ASSERT(controller_runtime_state_view.metrics.overrun_count == 1ul);
    TEST_ASSERT(controller_runtime_state_view.metrics.consecutive_overrun_count == 1ul);
    TEST_ASSERT(system_context_current_time_ms(system_context) == 300ul);

    controller_scheduler_linux_test_set_cycle_duration(controller_scheduler, 10ul);
    result = controller_scheduler_linux_test_inject_period(controller_scheduler, 1u);
    TEST_ASSERT(result.ok);
    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.metrics.cycle_count == 2ul);
    TEST_ASSERT(controller_runtime_state_view.metrics.consecutive_overrun_count == 0ul);
    TEST_ASSERT(system_context_current_time_ms(system_context) == 400ul);

    test_release_system_context(system_context);
    return 0;
}

