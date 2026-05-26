#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

int main(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    device_runtime_t system_context;
    scheduler_t *scheduler;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    scheduler = test_create_scheduler(system_context, 100ul);
    TEST_ASSERT(scheduler != 0);

    scheduler_linux_test_set_cycle_duration(scheduler, 250ul);
    result = scheduler_linux_test_inject_period(scheduler, 3u);
    TEST_ASSERT(result.ok);
    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.metrics.cycle_count == 1ul);
    TEST_ASSERT(app_state_view.metrics.overrun_count == 1ul);
    TEST_ASSERT(app_state_view.metrics.consecutive_overrun_count == 1ul);
    TEST_ASSERT(device_runtime_current_time_ms(system_context) == 300ul);

    scheduler_linux_test_set_cycle_duration(scheduler, 10ul);
    result = scheduler_linux_test_inject_period(scheduler, 1u);
    TEST_ASSERT(result.ok);
    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.metrics.cycle_count == 2ul);
    TEST_ASSERT(app_state_view.metrics.consecutive_overrun_count == 0ul);
    TEST_ASSERT(device_runtime_current_time_ms(system_context) == 400ul);

    test_release_system_context(system_context);
    return 0;
}

