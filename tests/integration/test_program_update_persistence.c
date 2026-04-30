#include "tests/test_support.h"

#include "application/use_cases/update_program_config.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = update_program_config_execute(&system_context, "./configs/programs/heavy_wash.json");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.wash_program.program_id, "heavy_wash") == 0);
    return 0;
}

