/**
 * @file    thread_registry.c
 * @brief   线程注册表实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/runtime/scheduler/thread_registry.h"
#include "framework/common/log.h"
#include <string.h>

static thread_entry_t s_entries[THREAD_REGISTRY_MAX];
static int            s_count = 0;

sw_err_t thread_register(const char *name, void *(*fn)(void *),
                         int sched_policy, int prio, size_t stack_size)
{
    if (s_count >= THREAD_REGISTRY_MAX)
    {
        LOG_ERROR("thread_registry: table full (max=%d)", THREAD_REGISTRY_MAX);
        return SW_ERR_OVERFLOW;
    }

    s_entries[s_count].name        = name;
    s_entries[s_count].fn          = fn;
    s_entries[s_count].sched_policy = sched_policy;
    s_entries[s_count].prio        = prio;
    s_entries[s_count].stack_size  = stack_size;
    s_count++;

    LOG_INFO("thread_registry: registered [%s] policy=%d prio=%d stack=%zu",
             name, sched_policy, prio, stack_size);
    return SW_OK;
}

int thread_registry_count(void)
{
    return s_count;
}

const thread_entry_t *thread_registry_get(int idx)
{
    if ((idx < 0) || (idx >= s_count))
    {
        return NULL;
    }
    return &s_entries[idx];
}
