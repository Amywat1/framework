#ifndef ADAPTERS_INBOUND_MAINTENANCE_COMMAND_ADAPTER_H
#define ADAPTERS_INBOUND_MAINTENANCE_COMMAND_ADAPTER_H

#include "application/coordinators/system_context.h"
#include "application/dto/command_dto.h"
#include "shared/result_types.h"

operation_result_t maintenance_command_adapter_dispatch(system_context_t *system_context, const command_dto_t *command_dto);

#endif

