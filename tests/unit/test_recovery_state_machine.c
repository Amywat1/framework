#include "tests/test_support.h"

#include "domain/services/recovery_state_machine.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = recovery_state_machine_execute(&system_context.actuator_port);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.stop_count == 1);
    return 0;
}

