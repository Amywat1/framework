#include "tests/test_support.h"

#include "adapters/inbound/maintenance_command_adapter.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    command_dto_t command_dto;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    memset(&command_dto, 0, sizeof(command_dto));
    command_dto.command_type = COMMAND_TYPE_ENTER_MAINTENANCE;
    result = maintenance_command_adapter_dispatch(&system_context, &command_dto);
    TEST_ASSERT(result.ok);

    command_dto.command_type = COMMAND_TYPE_JOG;
    command_dto.duration_ms = 500;
    result = maintenance_command_adapter_dispatch(&system_context, &command_dto);
    TEST_ASSERT(result.ok);
    return 0;
}

