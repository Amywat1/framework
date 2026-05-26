#include "tests/test_support.h"

#include "src/startup/app_bootstrap_private.h"

operation_result_t test_runtime_friend_read_device_runtime(app_t *app, device_runtime_t *device_runtime)
{
    return app_private_read_device_runtime(app, device_runtime);
}

scheduler_t *test_runtime_friend_scheduler(app_t *app)
{
    return app_private_scheduler(app);
}
