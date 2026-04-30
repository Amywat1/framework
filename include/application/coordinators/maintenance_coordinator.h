#ifndef APPLICATION_COORDINATORS_MAINTENANCE_COORDINATOR_H
#define APPLICATION_COORDINATORS_MAINTENANCE_COORDINATOR_H

#include "application/coordinators/system_context.h"
#include "application/dto/command_dto.h"
#include "shared/result_types.h"

operation_result_t maintenance_coordinator_dispatch(system_context_t *system_context, const command_dto_t *command_dto);

#endif

