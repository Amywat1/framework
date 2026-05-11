#include <string.h>

#include "application/coordinators/system_context.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_acquire_until_pool_full_and_recover(void)
{
    system_context_t acquired[32];
    system_context_t probe_handle;
    system_context_t stale_handle;
    operation_result_t result;
    unsigned int capacity;
    unsigned int index;

    memset(acquired, 0, sizeof(acquired));
    capacity = system_context_private_debug_capacity();
    TEST_ASSERT(capacity <= (sizeof(acquired) / sizeof(acquired[0])));
    TEST_ASSERT(system_context_private_debug_free_count() == capacity);

    for (index = 0u; index < capacity; ++index) {
        result = system_context_acquire(&acquired[index]);
        TEST_ASSERT(result.ok);
        TEST_ASSERT(acquired[index] != 0);
        TEST_ASSERT(system_context_private_debug_is_in_use(acquired[index]));
        TEST_ASSERT(system_context_private_debug_free_count() == capacity - index - 1u);
    }

    result = system_context_acquire(&acquired[capacity]);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);
    TEST_ASSERT(acquired[capacity] == 0);
    TEST_ASSERT(system_context_private_debug_free_count() == 0u);

    stale_handle = acquired[0];
    probe_handle = stale_handle;
    result = system_context_acquire(&probe_handle);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);
    TEST_ASSERT(probe_handle == 0);

    result = system_context_release(acquired[capacity - 1u]);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_debug_free_count() == 1u);

    result = system_context_acquire(&acquired[capacity - 1u]);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_debug_free_count() == 0u);

    for (index = 0u; index < capacity; ++index) {
        result = system_context_release(acquired[index]);
        TEST_ASSERT(result.ok);
    }
    TEST_ASSERT(system_context_private_debug_free_count() == capacity);
    return 0;
}

static int verify_release_restores_reacquire_without_locking_order(void)
{
    system_context_t first;
    system_context_t second;
    system_context_t released_first;
    system_context_t released_second;
    system_context_t reacquired;
    system_context_t reacquired_next;
    operation_result_t result;

    result = system_context_acquire(&first);
    TEST_ASSERT(result.ok);
    result = system_context_acquire(&second);
    TEST_ASSERT(result.ok);

    result = system_context_release(first);
    TEST_ASSERT(result.ok);
    result = system_context_release(second);
    TEST_ASSERT(result.ok);
    released_first = first;
    released_second = second;

    result = system_context_acquire(&reacquired);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(reacquired != released_first);
    TEST_ASSERT(reacquired != released_second);
    TEST_ASSERT(system_context_current_time_ms(reacquired) == 0ul);
    TEST_ASSERT(system_context_pending_trigger_count(reacquired) == 0u);
    TEST_ASSERT(!system_context_private_debug_is_in_use(released_first));
    TEST_ASSERT(!system_context_private_debug_is_in_use(released_second));

    result = system_context_acquire(&reacquired_next);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(reacquired_next != released_first);
    TEST_ASSERT(reacquired_next != released_second);
    TEST_ASSERT(reacquired_next != reacquired);
    TEST_ASSERT(system_context_current_time_ms(reacquired_next) == 0ul);
    TEST_ASSERT(system_context_pending_trigger_count(reacquired_next) == 0u);
    TEST_ASSERT(!system_context_private_debug_is_in_use(released_first));
    TEST_ASSERT(!system_context_private_debug_is_in_use(released_second));

    result = system_context_release(reacquired);
    TEST_ASSERT(result.ok);
    result = system_context_release(reacquired_next);
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_in_use_only_tracks_legality_not_free_list_ownership(void)
{
    system_context_t system_context;
    operation_result_t result;
    unsigned int free_count_before_reset;

    result = system_context_acquire(&system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_debug_is_in_use(system_context));

    free_count_before_reset = system_context_private_debug_free_count();
    result = system_context_reset(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_debug_is_in_use(system_context));
    TEST_ASSERT(system_context_private_debug_free_count() == free_count_before_reset);

    result = system_context_release(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!system_context_private_debug_is_in_use(system_context));
    return 0;
}

int main(void)
{
    if (verify_acquire_until_pool_full_and_recover() != 0) {
        return 1;
    }
    if (verify_release_restores_reacquire_without_locking_order() != 0) {
        return 1;
    }
    if (verify_in_use_only_tracks_legality_not_free_list_ownership() != 0) {
        return 1;
    }
    return 0;
}
