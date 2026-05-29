#include "tests/test_support.h"

static int verify_released_context_is_rejected_by_scheduler_boundary(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&driver_context);
    test_release_system_context();

    TEST_ASSERT(test_scheduler_create_unbound(0, 0) == 0);
    result = test_scheduler_read_bound_view(&app_state_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

int main(void)
{
    if (verify_released_context_is_rejected_by_scheduler_boundary() != 0)
    {
        return 1;
    }
    return 0;
}
