#ifndef PLATFORM_LINUX_MAIN_LOOP_H
#define PLATFORM_LINUX_MAIN_LOOP_H

#include "application/coordinators/system_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

operation_result_t main_loop_run(system_context_t *system_context, const char *program_id);
operation_result_t main_loop_submit_trigger(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event);
void main_loop_advance_time(system_context_t *system_context, unsigned long elapsed_ms);

#endif
