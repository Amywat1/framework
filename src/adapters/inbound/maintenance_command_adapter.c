#include "adapters/inbound/maintenance_command_adapter.h"

#include "application/coordinators/maintenance_coordinator.h"

operation_result_t maintenance_command_adapter_dispatch(system_context_t *system_context, const command_dto_t *command_dto)
{
    return maintenance_coordinator_dispatch(system_context, command_dto);
}

