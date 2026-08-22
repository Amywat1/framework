/**
 * @file    periodic_task.c
 * @brief   调度器周期任务注册辅助实现
 */

#include "runtime/scheduler/periodic_task.h"

#include "common/log.h"
#include "runtime/scheduler/thread_registry.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* 每个周期任务恰好占用一个线程登记槽，因此容量与线程表对齐：
 * 周期任务不再构成独立的第二道容量上限，超限统一由 thread_registry 报错。 */
#define PERIODIC_TASK_MAX THREAD_REGISTRY_MAX

/* 编译期断言：容量必须不超过线程表，否则 s_slots 会先于线程表耗尽而给出误导性错误。
 * 项目为 C99，无 _Static_assert，改用负数组宽度触发编译错误。 */
typedef char periodic_task_capacity_check_t[(PERIODIC_TASK_MAX <= THREAD_REGISTRY_MAX) ? 1 : -1];

#define PERIODIC_TASK_NS_PER_MS  1000000L
#define PERIODIC_TASK_NS_PER_SEC 1000000000L

typedef struct {
    bool                   used;
    const char            *name;
    uint32_t               period_ms;
    periodic_task_fn_t     fn;
    void                  *ctx;
    periodic_task_stats_t  stats;
} periodic_task_slot_t;

static periodic_task_slot_t s_slots[PERIODIC_TASK_MAX];
static pthread_mutex_t      s_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief  将绝对时间戳推进一个周期
 * @param  deadline  待推进的绝对时间（CLOCK_MONOTONIC）
 * @param  period_ms 周期毫秒数
 */
static void advance_deadline(struct timespec *deadline, uint32_t period_ms)
{
    deadline->tv_sec += (time_t)(period_ms / 1000U);
    deadline->tv_nsec += (long)(period_ms % 1000U) * PERIODIC_TASK_NS_PER_MS;
    if (deadline->tv_nsec >= PERIODIC_TASK_NS_PER_SEC) {
        deadline->tv_sec += 1;
        deadline->tv_nsec -= PERIODIC_TASK_NS_PER_SEC;
    }
}

/**
 * @brief  判断 a 是否已晚于或等于 b
 */
static bool deadline_reached(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec != b->tv_sec) {
        return a->tv_sec > b->tv_sec;
    }
    return a->tv_nsec >= b->tv_nsec;
}

uint32_t periodic_task_next_deadline(struct timespec *deadline, uint32_t period_ms, const struct timespec *now)
{
    uint32_t skipped = 0U;

    if ((deadline == NULL) || (now == NULL) || (period_ms == 0U)) {
        return 0U;
    }

    advance_deadline(deadline, period_ms);

    /* 推进后若仍不晚于 now，说明回调耗时超过一个周期：跳过已错过的拍而不追赶 */
    while (deadline_reached(now, deadline)) {
        advance_deadline(deadline, period_ms);
        skipped++;
    }

    return skipped;
}

#define PERIODIC_TASK_US_PER_SEC 1000000U
#define PERIODIC_TASK_NS_PER_US  1000L

/**
 * @brief  later - earlier 的微秒差；later 早于 earlier 时返回 0，超出 uint32 则饱和
 */
static uint32_t timespec_delta_us(const struct timespec *later, const struct timespec *earlier)
{
    int64_t  sec;
    int64_t  nsec;
    uint64_t us;

    if ((later == NULL) || (earlier == NULL)) {
        return 0U;
    }

    sec  = (int64_t)later->tv_sec - (int64_t)earlier->tv_sec;
    nsec = (int64_t)later->tv_nsec - (int64_t)earlier->tv_nsec;
    if (nsec < 0) {
        sec -= 1;
        nsec += PERIODIC_TASK_NS_PER_SEC;
    }
    if (sec < 0) {
        return 0U;
    }
    if (sec >= (int64_t)(UINT32_MAX / PERIODIC_TASK_US_PER_SEC)) {
        return UINT32_MAX;
    }

    us = ((uint64_t)sec * (uint64_t)PERIODIC_TASK_US_PER_SEC)
         + ((uint64_t)nsec / (uint64_t)PERIODIC_TASK_NS_PER_US);
    if (us > (uint64_t)UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)us;
}

void periodic_task_note_cycle(periodic_task_stats_t *stats,
                              uint32_t               skipped,
                              uint32_t               cb_us,
                              uint32_t               wake_late_us)
{
    if (stats == NULL) {
        return;
    }

    stats->run_count++;
    stats->skip_count += skipped;
    if (skipped > stats->skip_max) {
        stats->skip_max = skipped;
    }
    stats->last_cb_us = cb_us;
    if (cb_us > stats->max_cb_us) {
        stats->max_cb_us = cb_us;
    }
    stats->last_wake_late_us = wake_late_us;
    if (wake_late_us > stats->max_wake_late_us) {
        stats->max_wake_late_us = wake_late_us;
    }
}

/** @brief 一拍所需的槽位快照，避免 tick 循环中逐字段无锁读 */
typedef struct {
    periodic_task_fn_t fn;
    void              *ctx;
    const char        *name;
    uint32_t           period_ms;
} periodic_task_tick_view_t;

/**
 * @brief  在锁内取出本拍所需的槽位字段
 * @return true 槽位有效；false 表示已被清空，调用方须退出线程
 *
 * @note   为何要在锁内取：周期线程以 pthread_detach 创建、不可 join，
 *         `periodic_task_reset_for_test()` 清表并不会停下它们。此前 tick 循环
 *         直接解引用 `slot->fn`，于是「上一个用例启动的 10ms 线程 + 下一个用例
 *         setUp 里的清表」这一组合会让线程跳到空指针——ASan 报为 pc 0x0 的 SEGV，
 *         在 tests/runtime/test_scheduler.c 上实测约三成复现。
 *         `name` 与 `period_ms` 同样会被清表改写，故一并纳入同一次快照：
 *         线程要么看到完整的旧值、要么看到已清空并干净退出，不会读到半清状态。
 */
static bool periodic_task_take_tick_view(const periodic_task_slot_t *slot, periodic_task_tick_view_t *out)
{
    bool valid;

    pthread_mutex_lock(&s_mutex);
    valid = slot->used && (slot->fn != NULL) && (slot->period_ms > 0U);
    if (valid) {
        out->fn        = slot->fn;
        out->ctx       = slot->ctx;
        out->name      = slot->name;
        out->period_ms = slot->period_ms;
    }
    pthread_mutex_unlock(&s_mutex);
    return valid;
}

static void *periodic_task_thread_fn(void *arg)
{
    periodic_task_slot_t *slot = (periodic_task_slot_t *)arg;
    struct timespec       deadline;
    uint32_t              wake_late_us = 0U;
    bool                  slept        = false;

    /* 入口只判空指针；槽位字段的有效性由循环内的加锁快照负责，
     * 不在这里无锁读 fn / period_ms。 */
    if (slot == NULL) {
        return NULL;
    }

    /* 以绝对截止时间推进，避免"回调耗时累加进周期"的漂移。
     * 回调耗时超过一个周期时，跳过已错过的拍而不是无限追赶。 */
    (void)clock_gettime(CLOCK_MONOTONIC, &deadline);

    for (;;) {
        struct timespec           cb_start;
        struct timespec           cb_end;
        struct timespec           now;
        uint32_t                  skipped;
        uint32_t                  cb_us;
        periodic_task_tick_view_t view;

        if (!periodic_task_take_tick_view(slot, &view)) {
            /* 槽位已被清空（仅测试路径会发生）：干净退出，不再触碰 slot */
            return NULL;
        }

        (void)clock_gettime(CLOCK_MONOTONIC, &cb_start);
        view.fn(view.ctx);
        (void)clock_gettime(CLOCK_MONOTONIC, &cb_end);
        cb_us = timespec_delta_us(&cb_end, &cb_start);

        now     = cb_end;
        skipped = periodic_task_next_deadline(&deadline, view.period_ms, &now);

        pthread_mutex_lock(&s_mutex);
        periodic_task_note_cycle(&slot->stats, skipped, cb_us, slept ? wake_late_us : 0U);
        pthread_mutex_unlock(&s_mutex);

        if (skipped > 0U) {
            LOG_WARN("periodic_task: [%s] skipped %u tick(s) cb=%uus",
                     (view.name != NULL) ? view.name : "?",
                     (unsigned)skipped,
                     (unsigned)cb_us);
        }

        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) != 0) {
            /* 仅 EINTR 需要重试；其余错误无法通过重试恢复，退出线程交由上层诊断 */
            if (errno != EINTR) {
                LOG_ERROR("periodic_task: [%s] clock_nanosleep failed errno=%d",
                          (view.name != NULL) ? view.name : "?",
                          errno);
                return NULL;
            }
        }

        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        wake_late_us = timespec_delta_us(&now, &deadline);
        slept        = true;
    }
}

unsigned periodic_task_count(void)
{
    unsigned n = 0U;
    unsigned i;

    pthread_mutex_lock(&s_mutex);
    for (i = 0U; i < PERIODIC_TASK_MAX; i++) {
        if (s_slots[i].used) {
            n++;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return n;
}

sw_err_t periodic_task_get_stats(unsigned index, periodic_task_stats_t *out)
{
    unsigned seen = 0U;
    unsigned i;

    if (out == NULL) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    for (i = 0U; i < PERIODIC_TASK_MAX; i++) {
        if (!s_slots[i].used) {
            continue;
        }
        if (seen == index) {
            *out = s_slots[i].stats;
            pthread_mutex_unlock(&s_mutex);
            return SW_OK;
        }
        seen++;
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_ERR_NOT_FOUND;
}

void periodic_task_reset_for_test(void)
{
    pthread_mutex_lock(&s_mutex);
    (void)memset(s_slots, 0, sizeof(s_slots));
    pthread_mutex_unlock(&s_mutex);
}

sw_err_t periodic_task_register(const char        *name,
                                uint32_t           period_ms,
                                periodic_task_fn_t fn,
                                void              *ctx,
                                int                sched_policy,
                                int                prio,
                                size_t             stack_size)
{
    unsigned i;

    if ((name == NULL) || (fn == NULL) || (period_ms == 0U)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    for (i = 0U; i < PERIODIC_TASK_MAX; i++) {
        periodic_task_slot_t *slot = &s_slots[i];

        if (!slot->used) {
            sw_err_t ret;

            (void)memset(slot, 0, sizeof(*slot));
            slot->used             = true;
            slot->name             = name;
            slot->period_ms        = period_ms;
            slot->fn               = fn;
            slot->ctx              = ctx;
            slot->stats.name       = name;
            slot->stats.period_ms  = period_ms;

            pthread_mutex_unlock(&s_mutex);
            ret = thread_register_arg(name, periodic_task_thread_fn, slot, sched_policy, prio, stack_size);
            if (ret != SW_OK) {
                pthread_mutex_lock(&s_mutex);
                (void)memset(slot, 0, sizeof(*slot));
                pthread_mutex_unlock(&s_mutex);
                return ret;
            }

            LOG_INFO("periodic_task: registered [%s] period=%ums", name, (unsigned)period_ms);
            return SW_OK;
        }
    }

    pthread_mutex_unlock(&s_mutex);
    LOG_ERROR("periodic_task: table full (max=%u)", (unsigned)PERIODIC_TASK_MAX);
    return SW_ERR_OVERFLOW;
}
