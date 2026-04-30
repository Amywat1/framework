#ifndef PLATFORM_LINUX_MAIN_LOOP_H
#define PLATFORM_LINUX_MAIN_LOOP_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

operation_result_t main_loop_run(system_context_t *system_context, const char *program_id);

#endif

