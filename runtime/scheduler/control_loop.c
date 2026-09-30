/**
 * @file    control_loop.c
 * @brief   单线程 FIFO 控制环实现
 *
 * 所有控制类节拍共享一条低于急停优先级的 SCHED_FIFO 线程。
 * 节拍以 CLOCK_MONOTONIC 绝对截止时间推进，落后则跳拍不追赶。
 * 独立 FIFO 看门狗监视心跳；超时走注入绊索后 abort，不请求 hold。
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "runtime/scheduler/control_loop.h"

#include "common/log.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "runtime/config/thread_config.h"
#include "runtime/scheduler/thread_registry.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>

#define CONTROL_LOOP_NS_PER_SEC            1000000000L
#define CONTROL_LOOP_US_PER_SEC            1000000ULL
#define CONTROL_LOOP_NS_PER_US             1000ULL
#define CONTROL_LOOP_MS_PER_SEC            1000ULL
#define CONTROL_LOOP_NS_PER_MS             1000000ULL
#define CONTROL_LOOP_SKIP_WARN_INTERVAL_MS 2000U

typedef struct {
    bool                  used;
    const char           *name;
    uint32_t              period_ms;
    uint32_t              period_ticks;
    periodic_task_fn_t    fn;
    void                 *ctx;
    periodic_task_stats_t stats;
} control_loop_slot_t;

typedef struct {
    unsigned           slot_idx;
    periodic_task_fn_t fn;
    void              *ctx;
    uint32_t           cb_us;
} control_loop_tick_view_t;

static control_loop_slot_t             s_slots[CONTROL_LOOP_SLOT_MAX];
static pthread_mutex_t                 s_mutex;
static pthread_once_t                  s_mutex_once = PTHREAD_ONCE_INIT;
static bool                            s_thread_registered;
static uint32_t                        s_beat;
static uint64_t                        s_skip_warn_ms;
static atomic_uint_fast64_t            s_heartbeat_ms;
static control_loop_watchdog_trip_fn_t s_watchdog_trip_fn;

static void control_loop_mutex_init_once(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mutex);
}

static void control_loop_lock(void)
{
    (void)pthread_once(&s_mutex_once, control_loop_mutex_init_once);
    (void)pthread_mutex_lock(&s_mutex);
}

static void control_loop_unlock(void)
{
    (void)pthread_mutex_unlock(&s_mutex);
}

static void control_loop_apply_qos(void)
{
#ifdef __linux__
    (void)prctl(PR_SET_TIMERSLACK, 1UL);
#endif
    sw_log_mark_thread_async();
}

/** @brief 本拍单调毫秒，与 time_util_get_ms 同一时钟 */
static uint64_t control_loop_now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * CONTROL_LOOP_MS_PER_SEC)
           + ((uint64_t)ts.tv_nsec / CONTROL_LOOP_NS_PER_MS);
}

/** @brief 写入本拍心跳，供看门狗无锁读取 */
static void control_loop_note_beat(void)
{
    atomic_store_explicit(&s_heartbeat_ms, control_loop_now_ms(), memory_order_release);
}

/** @brief 把绝对截止时间向前推进 period_ms */
static void control_loop_deadline_add_ms(struct timespec *deadline, uint32_t period_ms)
{
    deadline->tv_nsec += (long)period_ms * (long)CONTROL_LOOP_NS_PER_MS;
    while (deadline->tv_nsec >= CONTROL_LOOP_NS_PER_SEC) {
        deadline->tv_sec += 1;
        deadline->tv_nsec -= CONTROL_LOOP_NS_PER_SEC;
    }
}

/** @brief 绝对时钟睡眠，EINTR 重试；其它错误返回正错误码 */
static int control_loop_sleep_abs(const struct timespec *deadline)
{
    int sleep_ret;

    while ((sleep_ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, deadline, NULL)) != 0) {
        if (sleep_ret != EINTR) {
            return sleep_ret;
        }
    }
    return 0;
}

/**
 * @brief 看门狗绊索：先走注入回调，再 abort
 * @note  不请求 output hold。回调为空时仍终止进程。
 */
static void control_loop_wd_trip(void)
{
    LOG_ERROR("control_loop_wd: heartbeat timeout, cutting out then abort");
    if (s_watchdog_trip_fn != NULL) {
        s_watchdog_trip_fn();
    }
    abort();
}

static void *control_loop_wd_thread_fn(void *arg)
{
    struct timespec deadline;
    uint64_t        started_ms;

    (void)arg;
    control_loop_apply_qos();
    started_ms = control_loop_now_ms();
    (void)clock_gettime(CLOCK_MONOTONIC, &deadline);

    for (;;) {
        uint64_t now_ms   = control_loop_now_ms();
        uint64_t beat_ms  = atomic_load_explicit(&s_heartbeat_ms, memory_order_acquire);
        uint64_t mark_ms  = (beat_ms == 0U) ? started_ms : beat_ms;
        uint32_t limit_ms = (beat_ms == 0U) ? THD_CONTROL_LOOP_WD_STARTUP_MS
                                            : THD_CONTROL_LOOP_WD_TIMEOUT_MS;
        uint32_t age_ms   = time_elapsed_ms(mark_ms, now_ms);
        int      sleep_ret;

        if (age_ms >= limit_ms) {
            LOG_ERROR("control_loop_wd: heartbeat timeout age=%ums limit=%ums",
                      (unsigned)age_ms,
                      (unsigned)limit_ms);
            control_loop_wd_trip();
        }

        control_loop_deadline_add_ms(&deadline, THD_CONTROL_LOOP_WD_POLL_MS);
        sleep_ret = control_loop_sleep_abs(&deadline);
        if (sleep_ret != 0) {
            LOG_ERROR("control_loop_wd: clock_nanosleep failed ret=%d", sleep_ret);
            control_loop_wd_trip();
        }
    }

    return NULL;
}

/** @brief from 到 to 的微秒差；to 早于 from 返回 0，超出 32 位饱和 */
static uint32_t elapsed_us(const struct timespec *from, const struct timespec *to)
{
    int64_t  sec  = (int64_t)to->tv_sec - (int64_t)from->tv_sec;
    int64_t  nsec = (int64_t)to->tv_nsec - (int64_t)from->tv_nsec;
    uint64_t us;

    if (nsec < 0) {
        sec -= 1;
        nsec += CONTROL_LOOP_NS_PER_SEC;
    }
    if (sec < 0) {
        return 0U;
    }
    us = ((uint64_t)sec * CONTROL_LOOP_US_PER_SEC) + ((uint64_t)nsec / CONTROL_LOOP_NS_PER_US);
    return (us > (uint64_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)us;
}

static unsigned used_count_locked(void)
{
    unsigned n = 0U;
    unsigned i;

    for (i = 0U; i < CONTROL_LOOP_SLOT_MAX; i++) {
        if (s_slots[i].used) {
            n++;
        }
    }
    return n;
}

static unsigned snapshot_due_slots(uint32_t beat, control_loop_tick_view_t *out, unsigned max_out)
{
    unsigned n = 0U;
    unsigned i;

    control_loop_lock();
    for (i = 0U; i < CONTROL_LOOP_SLOT_MAX; i++) {
        control_loop_slot_t *slot = &s_slots[i];

        if (!slot->used || (slot->fn == NULL) || (slot->period_ticks == 0U)) {
            continue;
        }
        if ((beat % slot->period_ticks) != 0U) {
            continue;
        }
        if (n >= max_out) {
            break;
        }
        out[n].slot_idx = i;
        out[n].fn       = slot->fn;
        out[n].ctx      = slot->ctx;
        out[n].cb_us    = 0U;
        n++;
    }
    control_loop_unlock();
    return n;
}

/* 调用者必须已持有 s_mutex */
static void note_skipped_beats_locked(uint32_t first_missed_beat, uint32_t skipped)
{
    uint32_t k;
    unsigned i;

    if (skipped == 0U) {
        return;
    }

    for (i = 0U; i < CONTROL_LOOP_SLOT_MAX; i++) {
        control_loop_slot_t *slot = &s_slots[i];
        uint32_t             missed_due = 0U;

        if (!slot->used || (slot->period_ticks == 0U)) {
            continue;
        }
        for (k = 0U; k < skipped; k++) {
            uint32_t beat = first_missed_beat + k;

            if ((beat % slot->period_ticks) == 0U) {
                missed_due++;
            }
        }
        if (missed_due == 0U) {
            continue;
        }
        slot->stats.skip_count += missed_due;
        if (missed_due > slot->stats.skip_max) {
            slot->stats.skip_max = missed_due;
        }
    }
}

/**
 * @brief 把本拍各回调观测写回对应槽位
 * @note  late_us 是整拍相对下一拍截止的越过量，同拍各回调共用；
 *        跳拍数另由 note_skipped_beats_locked() 按槽位周期计入。
 */
static void note_runs(const control_loop_tick_view_t *due,
                      unsigned                        due_count,
                      uint32_t                        wake_late_us,
                      uint32_t                        late_us)
{
    unsigned i;

    control_loop_lock();
    for (i = 0U; i < due_count; i++) {
        control_loop_slot_t *slot = &s_slots[due[i].slot_idx];

        if (!slot->used) {
            continue;
        }
        periodic_task_note_cycle(&slot->stats, 0U, due[i].cb_us, wake_late_us, late_us);
    }
    control_loop_unlock();
}

static void *control_loop_thread_fn(void *arg)
{
    struct timespec deadline;
    uint32_t        wake_late_us = 0U;
    bool            slept        = false;

    (void)arg;
    control_loop_apply_qos();
    (void)clock_gettime(CLOCK_MONOTONIC, &deadline);

    for (;;) {
        struct timespec          now;
        struct timespec          cb_start;
        struct timespec          cb_end;
        struct timespec          old_deadline;
        control_loop_tick_view_t due[CONTROL_LOOP_SLOT_MAX];
        unsigned                 due_count;
        unsigned                 i;
        uint32_t                 skipped;
        uint32_t                 late_us;
        uint32_t                 wake_us;
        uint32_t                 run_beat;
        int                      sleep_ret;
        bool                     warn_skip = false;

        control_loop_note_beat();
        control_loop_lock();
        if (used_count_locked() == 0U) {
            control_loop_unlock();
            return NULL;
        }
        run_beat = s_beat;
        control_loop_unlock();

        due_count = snapshot_due_slots(run_beat, due, CONTROL_LOOP_SLOT_MAX);
        wake_us   = slept ? wake_late_us : 0U;

        for (i = 0U; i < due_count; i++) {
            (void)clock_gettime(CLOCK_MONOTONIC, &cb_start);
            due[i].fn(due[i].ctx);
            (void)clock_gettime(CLOCK_MONOTONIC, &cb_end);
            due[i].cb_us = elapsed_us(&cb_start, &cb_end);
        }

        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        old_deadline = deadline;
        skipped      = periodic_task_next_deadline(&deadline, THD_MOTOR_TICK_PERIOD_MS, &now);
        late_us      = periodic_task_late_us(&old_deadline, &now, THD_MOTOR_TICK_PERIOD_MS);
        note_runs(due, due_count, wake_us, late_us);

        control_loop_lock();
        note_skipped_beats_locked(run_beat + 1U, skipped);
        s_beat = run_beat + 1U + skipped;
        if (periodic_task_skip_should_warn(skipped, 0U, THD_MOTOR_TICK_PERIOD_MS)) {
            uint64_t now_ms = ((uint64_t)now.tv_sec * CONTROL_LOOP_MS_PER_SEC)
                              + ((uint64_t)now.tv_nsec / CONTROL_LOOP_NS_PER_MS);

            if ((s_skip_warn_ms == 0U)
                || (time_elapsed_ms(s_skip_warn_ms, now_ms) >= CONTROL_LOOP_SKIP_WARN_INTERVAL_MS)) {
                s_skip_warn_ms = now_ms;
                warn_skip      = true;
            }
        }
        control_loop_unlock();

        if (warn_skip) {
            LOG_WARN("control_loop: skipped %u tick(s) period=%ums late=%uus wake_late=%uus",
                     (unsigned)skipped,
                     (unsigned)THD_MOTOR_TICK_PERIOD_MS,
                     (unsigned)late_us,
                     (unsigned)wake_us);
        }

        sleep_ret = control_loop_sleep_abs(&deadline);
        if (sleep_ret != 0) {
            LOG_ERROR("control_loop: clock_nanosleep failed ret=%d", sleep_ret);
            return NULL;
        }

        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        wake_late_us = elapsed_us(&deadline, &now);
        slept        = true;
    }
}

unsigned control_loop_count(void)
{
    unsigned n;

    control_loop_lock();
    n = used_count_locked();
    control_loop_unlock();
    return n;
}

sw_err_t control_loop_get_stats(unsigned index, periodic_task_stats_t *out)
{
    unsigned seen = 0U;
    unsigned i;

    if (out == NULL) {
        return SW_ERR_PARAM;
    }

    control_loop_lock();
    for (i = 0U; i < CONTROL_LOOP_SLOT_MAX; i++) {
        if (!s_slots[i].used) {
            continue;
        }
        if (seen == index) {
            *out = s_slots[i].stats;
            control_loop_unlock();
            return SW_OK;
        }
        seen++;
    }
    control_loop_unlock();
    return SW_ERR_NOT_FOUND;
}

void control_loop_set_watchdog_trip(control_loop_watchdog_trip_fn_t fn)
{
    s_watchdog_trip_fn = fn;
}

void control_loop_reset_for_test(void)
{
    control_loop_lock();
    (void)memset(s_slots, 0, sizeof(s_slots));
    s_thread_registered = false;
    s_beat              = 0U;
    s_skip_warn_ms      = 0U;
    s_watchdog_trip_fn  = NULL;
    atomic_store_explicit(&s_heartbeat_ms, 0U, memory_order_relaxed);
    control_loop_unlock();
}

sw_err_t control_loop_register(const char *name, uint32_t period_ms, periodic_task_fn_t fn, void *ctx)
{
    unsigned i;

    if ((name == NULL) || (fn == NULL) || (period_ms == 0U)) {
        return SW_ERR_PARAM;
    }
    if ((period_ms % THD_MOTOR_TICK_PERIOD_MS) != 0U) {
        LOG_ERROR("control_loop: [%s] period=%ums 不是 %ums 的整数倍",
                  name,
                  (unsigned)period_ms,
                  (unsigned)THD_MOTOR_TICK_PERIOD_MS);
        return SW_ERR_PARAM;
    }

    control_loop_lock();
    for (i = 0U; i < CONTROL_LOOP_SLOT_MAX; i++) {
        control_loop_slot_t *slot = &s_slots[i];

        if (!slot->used) {
            sw_err_t ret = SW_OK;

            (void)memset(slot, 0, sizeof(*slot));
            slot->used         = true;
            slot->name         = name;
            slot->period_ms    = period_ms;
            slot->period_ticks = period_ms / THD_MOTOR_TICK_PERIOD_MS;
            slot->fn           = fn;
            slot->ctx          = ctx;
            slot->stats.name   = name;
            slot->stats.period_ms = period_ms;

            if (!s_thread_registered) {
                control_loop_unlock();
                ret = thread_register("control_loop",
                                      control_loop_thread_fn,
                                      SCHED_FIFO,
                                      THD_CONTROL_LOOP_PRIO,
                                      THD_CONTROL_LOOP_STACK);
                if (ret == SW_OK) {
                    ret = thread_register("control_loop_wd",
                                          control_loop_wd_thread_fn,
                                          SCHED_FIFO,
                                          THD_CONTROL_LOOP_WD_PRIO,
                                          THD_CONTROL_LOOP_WD_STACK);
                }
                control_loop_lock();
                if (ret != SW_OK) {
                    (void)memset(slot, 0, sizeof(*slot));
                    control_loop_unlock();
                    return ret;
                }
                s_thread_registered = true;
            }

            control_loop_unlock();
            LOG_INFO("control_loop: registered [%s] period=%ums", name, (unsigned)period_ms);
            return SW_OK;
        }
    }

    control_loop_unlock();
    LOG_ERROR("control_loop: table full (max=%u)", (unsigned)CONTROL_LOOP_SLOT_MAX);
    return SW_ERR_OVERFLOW;
}
