#ifndef APPLICATION_USE_CASES_START_WASH_CYCLE_H
#define APPLICATION_USE_CASES_START_WASH_CYCLE_H

#include "application/coordinators/system_context.h"
#include "application/dto/command_dto.h"
#include "shared/result_types.h"

operation_result_t start_wash_cycle_execute(system_context_t *system_context, const command_dto_t *command_dto);

#endif

