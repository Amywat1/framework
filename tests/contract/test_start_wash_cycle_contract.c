#include "tests/test_support.h"

#include "application/dto/command_dto.h"
#include "application/use_cases/start_wash_cycle.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    command_dto_t command_dto;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    memset(&command_dto, 0, sizeof(command_dto));
    command_dto.command_type = COMMAND_TYPE_START;
    strncpy(command_dto.program_id, "standard_wash", sizeof(command_dto.program_id) - 1);

    result = start_wash_cycle_execute(&system_context, &command_dto);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_cycle.result_code == RESULT_CODE_SUCCESS);
    return 0;
}

