#ifndef APPLICATION_USE_CASES_ACKNOWLEDGE_FAULT_H
#define APPLICATION_USE_CASES_ACKNOWLEDGE_FAULT_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

operation_result_t acknowledge_fault_execute(system_context_t *system_context, const char *event_id);

#endif

