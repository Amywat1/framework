#include <string.h>

#include "application/coordinators/control_context.h"
#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

static int verify_single_instance_acquire_release_cycle(void)
{
    operation_result_t result;

    result = control_context_init();
    TEST_ASSERT(result.ok);

    result = control_context_init();
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);

    result = control_context_deinit();
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_release_invalidates_handle_until_reacquire(void)
{
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    result = control_context_init();
    TEST_ASSERT(result.ok);
    result = control_context_deinit();
    TEST_ASSERT(result.ok);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, "stale", "stale", 0ul);
    result = control_tick_submit_trigger(&wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = control_context_init();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_current_time_ms() == 0ul);
    TEST_ASSERT(control_context_pending_trigger_count() == 0u);

    result = control_context_deinit();
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_initialized_only_tracks_active_single_instance(void)
{
    operation_result_t result;

    result = control_context_init();
    TEST_ASSERT(result.ok);

    result = control_context_reset();
    TEST_ASSERT(result.ok);

    result = control_context_deinit();
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_init_state_is_completed_by_runtime_creation_path(void)
{
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    result = control_context_init();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_device_state() == DEVICE_STATE_INIT);
    result = control_context_deinit();
    TEST_ASSERT(result.ok);

    test_setup_system_context(&driver_context);
    result = query_wash_session_status_execute(&wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(control_context_private_device_state() == DEVICE_STATE_STOPPED);
    test_release_system_context();
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
    if (verify_initialized_only_tracks_active_single_instance() != 0) {
        return 1;
    }
    if (verify_init_state_is_completed_by_runtime_creation_path() != 0) {
        return 1;
    }
    return 0;
}
