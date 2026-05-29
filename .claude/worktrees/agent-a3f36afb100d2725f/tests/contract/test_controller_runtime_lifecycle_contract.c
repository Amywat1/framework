#include <stdint.h>

#include "application/coordinators/controller_runtime.h"
#include "tests/test_support.h"

int main(void)
{
    controller_runtime_t *forged_runtime;
    controller_runtime_status_view_t status_view;
    simulated_driver_context_t driver_context;
    controller_runtime_t *controller_runtime;
    operation_result_t result;

    result = test_create_runtime(&controller_runtime, &driver_context, 100ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime != 0);
    TEST_ASSERT(test_runtime_scheduler(controller_runtime) != 0);

    result = controller_runtime_read_state(controller_runtime, &status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(status_view.lifecycle_state == CONTROLLER_RUNTIME_STATE_CREATED);
    TEST_ASSERT(status_view.system_context_acquired);
    TEST_ASSERT(status_view.scheduler_created);
    TEST_ASSERT(!status_view.run_invoked);
    TEST_ASSERT(status_view.scheduler_view_available);

    result = controller_scheduler_linux_test_inject_exit(test_runtime_scheduler(controller_runtime), false);
    TEST_ASSERT(result.ok);

    result = controller_runtime_run(controller_runtime);
    TEST_ASSERT(result.ok);

    result = controller_runtime_read_state(controller_runtime, &status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(status_view.lifecycle_state == CONTROLLER_RUNTIME_STATE_TERMINATED);
    TEST_ASSERT(status_view.run_invoked);
    TEST_ASSERT(status_view.scheduler_view_available);
    TEST_ASSERT(status_view.scheduler_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED);

    result = controller_runtime_run(controller_runtime);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = controller_runtime_destroy(controller_runtime);
    TEST_ASSERT(result.ok);
    result = controller_runtime_destroy(controller_runtime);
    TEST_ASSERT(result.ok);

    result = controller_runtime_read_state(controller_runtime, &status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(status_view.lifecycle_state == CONTROLLER_RUNTIME_STATE_DESTROYED);
    TEST_ASSERT(!status_view.system_context_acquired);
    TEST_ASSERT(!status_view.scheduler_created);
    TEST_ASSERT(!status_view.scheduler_view_available);

    /* 构造一个非 runtime 正式句柄地址，验证 opaque 边界仍拒绝伪造值。 */
    forged_runtime = (controller_runtime_t *)(((uintptr_t)controller_runtime) ^ (((uintptr_t)1u) << 8u));
    result = controller_runtime_destroy(forged_runtime);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);
    result = controller_runtime_read_state(forged_runtime, &status_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);
    return 0;
}
