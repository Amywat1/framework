#include "tests/test_support.h"

#include "platform/linux/main_loop.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = main_loop_run(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.stop_count == 0);

    driver_context.sensor_snapshot.estop_active = true;
    result = main_loop_run(&system_context, "standard_wash");
    TEST_ASSERT(!result.ok);
    return 0;
}

