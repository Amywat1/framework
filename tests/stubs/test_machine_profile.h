/**
 * @file    test_machine_profile.h
 * @brief   单元测试用机型 profile（框架不提供默认 profile）
 */

#ifndef TESTS_STUBS_TEST_MACHINE_PROFILE_H
#define TESTS_STUBS_TEST_MACHINE_PROFILE_H

#include "domain/job/machine_profile.h"

#define TEST_PROFILE_MAX_QUEUED_ORDERS 8U
#define TEST_PROFILE_MAX_ORDER_AGE_MS  (30U * 60U * 1000U)

/**
 * @brief  注册测试用单车 profile：单作业、最后一单后 HOLD、中止即清障、停机清队
 * @return machine_profile_register 的返回值
 */
static inline sw_err_t test_machine_profile_register(void)
{
    const machine_profile_t profile = {
        .max_active_jobs         = 1U,
        .max_queued_orders       = TEST_PROFILE_MAX_QUEUED_ORDERS,
        .hold_after_last_job     = true,
        .clear_on_job_abort      = true,
        .clear_orders_on_stopped = true,
        .max_order_age_ms        = TEST_PROFILE_MAX_ORDER_AGE_MS,
    };

    return machine_profile_register(&profile);
}

#endif /* TESTS_STUBS_TEST_MACHINE_PROFILE_H */
