#ifndef APPLICATION_USE_CASES_JOG_ACTUATOR_H
#define APPLICATION_USE_CASES_JOG_ACTUATOR_H

#include "application/coordinators/system_context.h"
#include "application/dto/command_dto.h"
#include "shared/result_types.h"

operation_result_t jog_actuator_execute(system_context_t *system_context, const command_dto_t *command_dto);

#endif

