#ifndef APPLICATION_COORDINATORS_WASH_CYCLE_COORDINATOR_H
#define APPLICATION_COORDINATORS_WASH_CYCLE_COORDINATOR_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

operation_result_t wash_cycle_coordinator_run(system_context_t *system_context, const char *program_id);

#endif

