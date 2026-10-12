/**
 * @file    active_jobs.c
 * @brief   在洗作业占用表实现
 */

#include "domain/job/active_jobs.h"

#include "common/sw_mutex.h"
#include "domain/job/machine_profile.h"

#include <pthread.h>
#include <string.h>

static job_desc_t      s_jobs[JOB_ACTIVE_CAP];
static unsigned        s_count;
static pthread_mutex_t s_mu;
static pthread_once_t  s_mu_once = PTHREAD_ONCE_INIT;

static void jobs_mu_init(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mu);
}

static void jobs_lock(void)
{
    (void)pthread_once(&s_mu_once, jobs_mu_init);
    pthread_mutex_lock(&s_mu);
}

static void jobs_unlock(void)
{
    pthread_mutex_unlock(&s_mu);
}

void active_jobs_init(void)
{
    active_jobs_clear();
}

sw_err_t active_jobs_add(const job_desc_t *job)
{
    machine_profile_t profile;

    if ((job == NULL) || (job->job_id == 0U)) {
        return SW_ERR_PARAM;
    }

    profile = machine_profile_get();
    jobs_lock();
    if (s_count >= (unsigned)profile.max_active_jobs) {
        jobs_unlock();
        return SW_ERR_OVERFLOW;
    }
    s_jobs[s_count] = *job;
    s_count++;
    jobs_unlock();
    return SW_OK;
}

sw_err_t active_jobs_remove(uint64_t job_id)
{
    unsigned i;

    if (job_id == 0U) {
        return SW_ERR_PARAM;
    }

    jobs_lock();
    for (i = 0U; i < s_count; i++) {
        if (s_jobs[i].job_id == job_id) {
            if (i + 1U < s_count) {
                memmove(&s_jobs[i], &s_jobs[i + 1U], (s_count - i - 1U) * sizeof(s_jobs[0]));
            }
            s_count--;
            memset(&s_jobs[s_count], 0, sizeof(s_jobs[0]));
            jobs_unlock();
            return SW_OK;
        }
    }
    jobs_unlock();
    return SW_ERR_STATE;
}

void active_jobs_clear(void)
{
    jobs_lock();
    memset(s_jobs, 0, sizeof(s_jobs));
    s_count = 0U;
    jobs_unlock();
}

unsigned active_jobs_count(void)
{
    unsigned count;

    jobs_lock();
    count = s_count;
    jobs_unlock();
    return count;
}

sw_err_t active_jobs_get_at(unsigned index, job_desc_t *out)
{
    if (out == NULL) {
        return SW_ERR_PARAM;
    }

    jobs_lock();
    if (index >= s_count) {
        jobs_unlock();
        return SW_ERR_PARAM;
    }
    *out = s_jobs[index];
    jobs_unlock();
    return SW_OK;
}

bool active_jobs_contains_order(uint64_t order_id)
{
    unsigned i;
    bool     found = false;

    if (order_id == 0U) {
        return false;
    }

    jobs_lock();
    for (i = 0U; i < s_count; i++) {
        if (s_jobs[i].order_id == order_id) {
            found = true;
            break;
        }
    }
    jobs_unlock();
    return found;
}

unsigned active_jobs_copy(job_desc_t *out, unsigned cap)
{
    unsigned count;

    jobs_lock();
    count = s_count;
    if ((out != NULL) && (cap > 0U) && (count > 0U)) {
        unsigned n = (count < cap) ? count : cap;

        memcpy(out, s_jobs, n * sizeof(s_jobs[0]));
    }
    jobs_unlock();
    return count;
}
