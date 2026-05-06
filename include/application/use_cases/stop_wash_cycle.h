#ifndef APPLICATION_USE_CASES_STOP_WASH_CYCLE_H
#define APPLICATION_USE_CASES_STOP_WASH_CYCLE_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

operation_result_t stop_wash_cycle_execute(system_context_t *system_context);
operation_result_t stop_wash_cycle_with_reason_execute(system_context_t *system_context, const char *reason_code);

#endif
