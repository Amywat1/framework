/**
 * @file    machine_profile.c
 * @brief   机型作业 profile 实现
 */

#include "domain/job/machine_profile.h"

#include "common/sw_mutex.h"

#include <pthread.h>

static const machine_profile_t s_default_profile = {
    .max_active_jobs      = 1U,
    .max_queued_orders    = 8U,
    .hold_after_last_job  = true,
    .clear_on_job_abort   = true,
    .max_order_age_ms     = 30U * 60U * 1000U,
};

static machine_profile_t s_profile;
static bool              s_inited;
static pthread_mutex_t   s_mu;
static pthread_once_t    s_mu_once = PTHREAD_ONCE_INIT;

static void profile_mu_init(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mu);
}

static bool profile_valid(const machine_profile_t *profile)
{
    if (profile == NULL) {
        return false;
    }
    if ((profile->max_active_jobs == 0U) || (profile->max_active_jobs > JOB_ACTIVE_CAP)) {
        return false;
    }
    if ((profile->max_queued_orders == 0U) || (profile->max_queued_orders > JOB_ORDER_QUEUE_CAP)) {
        return false;
    }
    return true;
}

void machine_profile_init(void)
{
    (void)pthread_once(&s_mu_once, profile_mu_init);
    pthread_mutex_lock(&s_mu);
    if (!s_inited) {
        s_profile = s_default_profile;
        s_inited  = true;
    }
    pthread_mutex_unlock(&s_mu);
}

sw_err_t machine_profile_register(const machine_profile_t *profile)
{
    if (!profile_valid(profile)) {
        return SW_ERR_PARAM;
    }

    (void)pthread_once(&s_mu_once, profile_mu_init);
    pthread_mutex_lock(&s_mu);
    s_profile = *profile;
    s_inited  = true;
    pthread_mutex_unlock(&s_mu);
    return SW_OK;
}

machine_profile_t machine_profile_get(void)
{
    machine_profile_t out;

    (void)pthread_once(&s_mu_once, profile_mu_init);
    pthread_mutex_lock(&s_mu);
    out = s_inited ? s_profile : s_default_profile;
    pthread_mutex_unlock(&s_mu);
    return out;
}

void machine_profile_reset_for_test(void)
{
    (void)pthread_once(&s_mu_once, profile_mu_init);
    pthread_mutex_lock(&s_mu);
    s_profile = s_default_profile;
    s_inited  = true;
    pthread_mutex_unlock(&s_mu);
}
