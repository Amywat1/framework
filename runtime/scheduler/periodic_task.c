/**
 * @file    periodic_task.c
 * @brief   调度器周期任务注册辅助实现
 */

#include "runtime/scheduler/periodic_task.h"

#include "common/log.h"
#include "runtime/scheduler/thread_registry.h"

#include <errno.h>
#include <stdbool.h>
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
    bool               used;
    const char        *name;
    uint32_t           period_ms;
    periodic_task_fn_t fn;
    void              *ctx;
} periodic_task_slot_t;

static periodic_task_slot_t s_slots[PERIODIC_TASK_MAX];

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

static void *periodic_task_thread_fn(void *arg)
{
    periodic_task_slot_t *slot = (periodic_task_slot_t *)arg;
    struct timespec       deadline;

    if ((slot == NULL) || (slot->fn == NULL) || (slot->period_ms == 0U)) {
        return NULL;
    }

    /* 以绝对截止时间推进，避免"回调耗时累加进周期"的漂移。
     * 回调耗时超过一个周期时，跳过已错过的拍而不是无限追赶。 */
    (void)clock_gettime(CLOCK_MONOTONIC, &deadline);

    for (;;) {
        struct timespec now;

        slot->fn(slot->ctx);

        advance_deadline(&deadline, slot->period_ms);

        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        while (deadline_reached(&now, &deadline)) {
            advance_deadline(&deadline, slot->period_ms);
        }

        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) != 0) {
            /* 仅 EINTR 需要重试；其余错误无法通过重试恢复，退出线程交由上层诊断 */
            if (errno != EINTR) {
                LOG_ERROR("periodic_task: [%s] clock_nanosleep failed errno=%d", slot->name, errno);
                return NULL;
            }
        }
    }
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

    for (i = 0U; i < PERIODIC_TASK_MAX; i++) {
        periodic_task_slot_t *slot = &s_slots[i];

        if (!slot->used) {
            (void)memset(slot, 0, sizeof(*slot));
            slot->used      = true;
            slot->name      = name;
            slot->period_ms = period_ms;
            slot->fn        = fn;
            slot->ctx       = ctx;

            {
                sw_err_t ret;

                ret = thread_register_arg(name, periodic_task_thread_fn, slot, sched_policy, prio, stack_size);
                if (ret != SW_OK) {
                    (void)memset(slot, 0, sizeof(*slot));
                    return ret;
                }
            }

            LOG_INFO("periodic_task: registered [%s] period=%ums", name, (unsigned)period_ms);
            return SW_OK;
        }
    }

    LOG_ERROR("periodic_task: table full (max=%u)", (unsigned)PERIODIC_TASK_MAX);
    return SW_ERR_OVERFLOW;
}
