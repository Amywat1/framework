#include "platform/linux/main_loop.h"

#include "application/coordinators/wash_cycle_coordinator.h"

operation_result_t main_loop_run(system_context_t *system_context, const char *program_id)
{
    return wash_cycle_coordinator_run(system_context, program_id);
}

