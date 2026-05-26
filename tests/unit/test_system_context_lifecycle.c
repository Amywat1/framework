#include <string.h>

#include "application/coordinators/device_runtime.h"
#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

static int verify_single_instance_acquire_release_cycle(void)
{
    device_runtime_t first_handle;
    device_runtime_t second_handle;
    operation_result_t result;

    result = device_runtime_acquire(&first_handle);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(first_handle != 0);
    TEST_ASSERT(device_runtime_private_debug_is_in_use(first_handle));

    result = device_runtime_acquire(&second_handle);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);
    TEST_ASSERT(second_handle == 0);

    result = device_runtime_release(first_handle);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!device_runtime_private_debug_is_in_use(first_handle));
    return 0;
}

static int verify_release_invalidates_handle_until_reacquire(void)
{
    device_runtime_t first;
    device_runtime_t reacquired;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    result = device_runtime_acquire(&first);
    TEST_ASSERT(result.ok);
    result = device_runtime_release(first);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!device_runtime_private_debug_is_in_use(first));

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, "stale", "stale", 0ul);
    result = control_tick_submit_trigger(first, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = device_runtime_acquire(&reacquired);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(reacquired == first);
    TEST_ASSERT(device_runtime_current_time_ms(reacquired) == 0ul);
    TEST_ASSERT(device_runtime_pending_trigger_count(reacquired) == 0u);
    TEST_ASSERT(device_runtime_private_debug_is_in_use(first));

    result = device_runtime_release(reacquired);
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_in_use_only_tracks_active_single_instance(void)
{
    device_runtime_t system_context;
    operation_result_t result;

    result = device_runtime_acquire(&system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_debug_is_in_use(system_context));

    result = device_runtime_reset(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_debug_is_in_use(system_context));

    result = device_runtime_release(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!device_runtime_private_debug_is_in_use(system_context));
    return 0;
}

static int verify_init_state_is_completed_by_runtime_creation_path(void)
{
    simulated_driver_context_t driver_context;
    device_runtime_t system_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    result = device_runtime_acquire(&system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_device_state(system_context) == DEVICE_STATE_INIT);
    result = device_runtime_release(system_context);
    TEST_ASSERT(result.ok);

    test_setup_system_context(&system_context, &driver_context);
    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(device_runtime_private_device_state(system_context) == DEVICE_STATE_STOPPED);
    test_release_system_context(system_context);
    return 0;
}

int main(void)
{
    if (verify_single_instance_acquire_release_cycle() != 0) {
        return 1;
    }
    if (verify_release_invalidates_handle_until_reacquire() != 0) {
        return 1;
    }
    if (verify_in_use_only_tracks_active_single_instance() != 0) {
        return 1;
    }
    if (verify_init_state_is_completed_by_runtime_creation_path() != 0) {
        return 1;
    }
    return 0;
}
