#include <string.h>

#include "application/coordinators/controller_runtime.h"
#include "tests/test_support.h"

int main(void)
{
    controller_runtime_status_view_t first_status_view;
    controller_runtime_status_view_t second_status_view;
    controller_runtime_t *retired_runtimes[20];
    simulated_driver_context_t first_driver_context;
    simulated_driver_context_t second_driver_context;
    simulated_driver_context_t recycled_driver_context;
    controller_runtime_t *first_runtime;
    controller_runtime_t *second_runtime;
    system_context_t first_system_context;
    unsigned int index;
    operation_result_t result;

    memset(retired_runtimes, 0, sizeof(retired_runtimes));

    result = test_create_runtime(&first_runtime, &first_driver_context, 100ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(first_runtime != 0);
    result = test_runtime_system_context(first_runtime, &first_system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(first_system_context != 0);

    result = test_create_runtime(&second_runtime, &second_driver_context, 100ul);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);

    result = controller_scheduler_linux_test_inject_exit(test_runtime_scheduler(first_runtime), false);
    TEST_ASSERT(result.ok);
    result = controller_runtime_run(first_runtime);
    TEST_ASSERT(result.ok);

    result = test_create_runtime(&second_runtime, &second_driver_context, 100ul);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);

    result = controller_runtime_destroy(first_runtime);
    TEST_ASSERT(result.ok);

    result = test_create_runtime(&second_runtime, &second_driver_context, 100ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(second_runtime != 0);

    result = controller_runtime_read_state(first_runtime, &first_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(first_status_view.lifecycle_state == CONTROLLER_RUNTIME_STATE_DESTROYED);
    TEST_ASSERT(!first_status_view.system_context_acquired);
    TEST_ASSERT(!first_status_view.scheduler_created);
    TEST_ASSERT(!first_status_view.scheduler_view_available);
    TEST_ASSERT(test_runtime_scheduler(first_runtime) == 0);
    result = test_runtime_system_context(first_runtime, &first_system_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = controller_runtime_destroy(first_runtime);
    TEST_ASSERT(result.ok);
    result = controller_runtime_read_state(second_runtime, &second_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(second_status_view.lifecycle_state == CONTROLLER_RUNTIME_STATE_CREATED);
    TEST_ASSERT(second_status_view.system_context_acquired);
    TEST_ASSERT(second_status_view.scheduler_created);

    result = controller_runtime_destroy(second_runtime);
    TEST_ASSERT(result.ok);

    for (index = 0u; index < (sizeof(retired_runtimes) / sizeof(retired_runtimes[0])); ++index) {
        result = test_create_runtime(&retired_runtimes[index], &recycled_driver_context, 100ul);
        TEST_ASSERT(result.ok);
        TEST_ASSERT(retired_runtimes[index] != 0);
        result = controller_runtime_destroy(retired_runtimes[index]);
        TEST_ASSERT(result.ok);
    }

    result = controller_runtime_read_state(first_runtime, &first_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(first_status_view.lifecycle_state == CONTROLLER_RUNTIME_STATE_DESTROYED);
    TEST_ASSERT(!first_status_view.system_context_acquired);
    TEST_ASSERT(!first_status_view.scheduler_created);
    TEST_ASSERT(!first_status_view.scheduler_view_available);
    result = controller_runtime_destroy(first_runtime);
    TEST_ASSERT(result.ok);
    return 0;
}
