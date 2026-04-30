#include "tests/test_support.h"

#include "application/coordinators/wash_cycle_coordinator.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = wash_cycle_coordinator_run(&system_context, "standard_wash");

    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.move_count >= 2);
    TEST_ASSERT(driver_context.roof_brush_count >= 2);
    TEST_ASSERT(driver_context.side_brush_count >= 2);
    TEST_ASSERT(system_context.wash_cycle.result_code == RESULT_CODE_SUCCESS);
    return 0;
}

