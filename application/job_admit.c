/**
 * @file    job_admit.c
 * @brief   作业放行核实现
 */

#include "application/job_admit.h"

#include "common/log.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "domain/job/active_jobs.h"
#include "domain/job/machine_profile.h"
#include "domain/job/order_book.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/ports/outbound/safety/safety_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"

#include <pthread.h>

static pthread_mutex_t s_mu;
static pthread_once_t  s_mu_once = PTHREAD_ONCE_INIT;
static uint64_t        s_next_job_id = 1U;

static void admit_mu_init(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mu);
}

static void admit_lock(void)
{
    (void)pthread_once(&s_mu_once, admit_mu_init);
    pthread_mutex_lock(&s_mu);
}

static void admit_unlock(void)
{
    pthread_mutex_unlock(&s_mu);
}

static uint64_t alloc_job_id(void)
{
    uint64_t id = s_next_job_id++;

    if (id == 0U) {
        id            = 1U;
        s_next_job_id = 2U;
    }
    return id;
}

static bool order_expired(const job_desc_t *job, uint32_t max_age_ms, uint64_t now_ms)
{
    if ((max_age_ms == 0U) || (job->enqueued_ms == 0U) || (now_ms < job->enqueued_ms)) {
        return false;
    }
    return (now_ms - job->enqueued_ms) > (uint64_t)max_age_ms;
}

/* 入簿时刻单调递增，队首未过期则其后均未过期。 */
static void drop_expired_heads(const machine_profile_t *profile)
{
    job_desc_t head;
    uint64_t   now_ms = time_util_get_ms();

    while (order_book_peek_front(&head) == SW_OK) {
        if (!order_expired(&head, profile->max_order_age_ms, now_ms)) {
            return;
        }
        (void)order_book_pop_front(&head);
        LOG_WARN("job_admit: drop expired order=%llu age_ms=%llu",
                 (unsigned long long)head.order_id,
                 (unsigned long long)(now_ms - head.enqueued_ms));
    }
}

/* 调用方须持 s_mu。start_job 在锁内调用：契约要求其只启动异步流程并立即返回。 */
static sw_err_t admit_locked(void)
{
    const device_ops_t *ops;
    machine_profile_t   profile;
    job_desc_t          job;
    sw_err_t            ret;

    /* can_admit 已含总开关、急停与 STANDBY/ACTIVE；切断未确认与 blocking 不在其中。 */
    if (safety_cutout_is_unconfirmed() || !op_mode_can_admit() || alarm_registry_has_blocking_active()) {
        return SW_OK;
    }

    profile = machine_profile_get();
    drop_expired_heads(&profile);
    if (active_jobs_count() >= (unsigned)profile.max_active_jobs) {
        return SW_OK;
    }
    if (order_book_peek_front(&job) != SW_OK) {
        return SW_OK;
    }

    ops = device_ops_get();
    if ((ops == NULL) || (ops->start_job == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    if ((ops->is_admit_ready != NULL) && !ops->is_admit_ready()) {
        return SW_OK;
    }

    job.job_id = alloc_job_id();
    ret        = active_jobs_add(&job);
    if (ret != SW_OK) {
        return ret;
    }

    ret = ops->start_job(&job);
    if (ret != SW_OK) {
        (void)active_jobs_remove(job.job_id);
        if (ret == SW_ERR_PARAM) {
            (void)order_book_cancel(job.order_id);
            LOG_ERROR("job_admit: start_job rejected, drop order=%llu mode=%u",
                      (unsigned long long)job.order_id,
                      (unsigned)job.mode);
        } else {
            LOG_ERROR("job_admit: start_job failed ret=%d order=%llu, keep queued",
                      (int)ret,
                      (unsigned long long)job.order_id);
        }
        return SW_OK;
    }

    (void)order_book_cancel(job.order_id);
    op_mode_on_job_started();
    return SW_OK;
}

/* 调用方须持 s_mu。仅可放行且簿空时自建；在洗占用不挡自建，放行仍受占用上限约束。 */
static sw_err_t generate_if_empty(wash_mode_t mode)
{
    job_desc_t generated = {0};

    if (!op_mode_can_admit() || (order_book_count() != 0U)) {
        return SW_OK;
    }
    generated.mode = mode;
    return order_book_enqueue(&generated, NULL);
}

sw_err_t job_admit_init(void)
{
    admit_lock();
    machine_profile_init();
    order_book_init();
    active_jobs_init();
    s_next_job_id = 1U;
    admit_unlock();
    LOG_INFO("job_admit: init ok");
    return SW_OK;
}

sw_err_t job_admit_submit(const job_desc_t *order)
{
    machine_profile_t profile;
    sw_err_t          ret;

    if (order == NULL) {
        return SW_ERR_PARAM;
    }

    admit_lock();
    if ((order->order_id != 0U) && active_jobs_contains_order(order->order_id)) {
        admit_unlock();
        return SW_OK;
    }
    profile = machine_profile_get();
    drop_expired_heads(&profile);
    ret = order_book_enqueue(order, NULL);
    admit_unlock();
    return ret;
}

sw_err_t job_admit_start(wash_mode_t mode)
{
    unsigned          before;
    machine_profile_t profile;
    sw_err_t          ret;

    admit_lock();
    profile = machine_profile_get();
    drop_expired_heads(&profile);
    ret = generate_if_empty(mode);
    if (ret == SW_OK) {
        before = active_jobs_count();
        ret    = admit_locked();
        if ((ret == SW_OK) && (active_jobs_count() > before)) {
            admit_unlock();
            return SW_OK;
        }
        if (ret != SW_ERR_NOT_INIT) {
            ret = SW_ERR_STATE;
        }
    }
    admit_unlock();
    return ret;
}

sw_err_t job_admit_cancel(uint64_t order_id)
{
    sw_err_t ret;

    admit_lock();
    ret = order_book_cancel(order_id);
    admit_unlock();
    return ret;
}

unsigned job_admit_drop_jobs(bool clear_orders)
{
    unsigned dropped;

    admit_lock();
    dropped = active_jobs_count();
    active_jobs_clear();
    if (clear_orders) {
        order_book_clear();
    }
    admit_unlock();
    return dropped;
}

void job_admit_clear_orders(void)
{
    admit_lock();
    order_book_clear();
    admit_unlock();
}

void job_admit_on_completed(uint64_t job_id)
{
    admit_lock();
    if (job_id == 0U) {
        active_jobs_clear();
    } else {
        (void)active_jobs_remove(job_id);
    }
    if (active_jobs_count() == 0U) {
        op_mode_on_last_job_finished();
    }
    admit_unlock();
}

void job_admit_on_job_aborted(uint64_t job_id)
{
    machine_profile_t profile;

    admit_lock();
    if (active_jobs_remove(job_id) != SW_OK) {
        admit_unlock();
        return;
    }

    profile = machine_profile_get();
    if (profile.clear_on_job_abort) {
        active_jobs_clear();
        /* 安全/全停/清障路径先撤占用，其后到达的中止已在上方被忽略；此处只剩工艺失败，具体原因见项目日志。 */
        op_mode_on_clear_requested(WASH_ABORT_INTERNAL);
        admit_unlock();
        return;
    }

    if (active_jobs_count() == 0U) {
        op_mode_on_last_job_finished();
    }
    admit_unlock();
}

void job_admit_on_aborted(wash_abort_cause_t cause)
{
    admit_lock();
    active_jobs_clear();
    op_mode_on_clear_requested(cause);
    admit_unlock();
}
