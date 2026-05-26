#include <stdint.h>

#include "startup/app_bootstrap.h"
#include "tests/test_support.h"

int main(void)
{
    app_t *forged_runtime;
    app_status_view_t status_view;
    simulated_driver_context_t driver_context;
    app_t *app;
    operation_result_t result;

    result = test_create_runtime(&app, &driver_context, 100ul);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app != 0);
    TEST_ASSERT(test_runtime_scheduler(app) != 0);

    result = app_read_state(app, &status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(status_view.state == APP_STATE_CREATED);
    TEST_ASSERT(status_view.device_runtime_acquired);
    TEST_ASSERT(status_view.scheduler_created);
    TEST_ASSERT(status_view.state == APP_STATE_CREATED);
    TEST_ASSERT(status_view.scheduler_view_available);

    result = scheduler_linux_test_inject_exit(test_runtime_scheduler(app), false);
    TEST_ASSERT(result.ok);

    result = app_run(app);
    TEST_ASSERT(result.ok);

    result = app_read_state(app, &status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(status_view.state == APP_STATE_TERMINATED);
    TEST_ASSERT(status_view.state == APP_STATE_TERMINATED);
    TEST_ASSERT(status_view.scheduler_view_available);
    TEST_ASSERT(status_view.scheduler_view.runtime_state == SCHEDULER_RUNTIME_STATE_STOPPED);

    result = app_run(app);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = app_destroy(app);
    TEST_ASSERT(result.ok);
    result = app_destroy(app);
    TEST_ASSERT(result.ok);

    result = app_read_state(app, &status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(status_view.state == APP_STATE_DESTROYED);
    TEST_ASSERT(!status_view.device_runtime_acquired);
    TEST_ASSERT(!status_view.scheduler_created);
    TEST_ASSERT(!status_view.scheduler_view_available);

    /* 构造一个非 runtime 正式句柄地址，验证 opaque 边界仍拒绝伪造值。 */
    forged_runtime = (app_t *)(((uintptr_t)app) ^ (((uintptr_t)1u) << 8u));
    result = app_destroy(forged_runtime);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);
    result = app_read_state(forged_runtime, &status_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);
    return 0;
}
