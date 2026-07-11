/**
 * @file    periodic_task.c
 * @brief   调度器周期任务注册辅助实现
 */

#include "runtime/scheduler/periodic_task.h"

#include "common/log.h"
#include "runtime/scheduler/thread_registry.h"

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#define PERIODIC_TASK_MAX 8U

typedef struct {
    bool               used;
    const char        *name;
    uint32_t           period_ms;
    periodic_task_fn_t fn;
    void              *ctx;
} periodic_task_slot_t;

static periodic_task_slot_t s_slots[PERIODIC_TASK_MAX];

static void *periodic_task_thread_fn(void *arg)
{
    periodic_task_slot_t *slot = (periodic_task_slot_t *)arg;

    if ((slot == NULL) || (slot->fn == NULL) || (slot->period_ms == 0U)) {
        return NULL;
    }

    for (;;) {
        slot->fn(slot->ctx);
        usleep((unsigned long)slot->period_ms * 1000UL);
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
