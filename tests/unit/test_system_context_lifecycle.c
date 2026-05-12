#include <string.h>

#include "application/coordinators/system_context.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_single_instance_acquire_release_cycle(void)
{
    system_context_t first_handle;
    system_context_t second_handle;
    operation_result_t result;

    result = system_context_acquire(&first_handle);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(first_handle != 0);
    TEST_ASSERT(system_context_private_debug_is_in_use(first_handle));

    result = system_context_acquire(&second_handle);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);
    TEST_ASSERT(second_handle == 0);

    result = system_context_release(first_handle);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!system_context_private_debug_is_in_use(first_handle));
    return 0;
}

static int verify_release_invalidates_stale_handle_and_reissues_clean_instance(void)
{
    system_context_t first;
    system_context_t reacquired;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    result = system_context_acquire(&first);
    TEST_ASSERT(result.ok);
    result = system_context_release(first);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!system_context_private_debug_is_in_use(first));

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, "stale", "stale", 0ul);
    result = main_loop_submit_trigger(first, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = system_context_acquire(&reacquired);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(reacquired != first);
    TEST_ASSERT(system_context_current_time_ms(reacquired) == 0ul);
    TEST_ASSERT(system_context_pending_trigger_count(reacquired) == 0u);
    TEST_ASSERT(!system_context_private_debug_is_in_use(first));

    result = system_context_release(reacquired);
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_in_use_only_tracks_active_single_instance(void)
{
    system_context_t system_context;
    operation_result_t result;

    result = system_context_acquire(&system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_debug_is_in_use(system_context));

    result = system_context_reset(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_debug_is_in_use(system_context));

    result = system_context_release(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!system_context_private_debug_is_in_use(system_context));
    return 0;
}

int main(void)
{
    if (verify_single_instance_acquire_release_cycle() != 0) {
        return 1;
    }
    if (verify_release_invalidates_stale_handle_and_reissues_clean_instance() != 0) {
        return 1;
    }
    if (verify_in_use_only_tracks_active_single_instance() != 0) {
        return 1;
    }
    return 0;
}
