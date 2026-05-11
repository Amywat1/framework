#include "tests/test_support.h"

#include "src/application/coordinators/controller_runtime_private.h"

operation_result_t test_runtime_friend_read_system_context(controller_runtime_t *controller_runtime,
    system_context_t *system_context)
{
    return controller_runtime_private_read_system_context(controller_runtime, system_context);
}

controller_scheduler_t *test_runtime_friend_scheduler(controller_runtime_t *controller_runtime)
{
    return controller_runtime_private_scheduler(controller_runtime);
}
