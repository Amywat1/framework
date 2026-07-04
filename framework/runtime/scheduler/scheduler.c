/**
 * @file    scheduler.c
 * @brief   线程调度器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/runtime/scheduler/scheduler.h"
#include "framework/runtime/scheduler/thread_registry.h"
#include "framework/common/log.h"
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>

sw_err_t scheduler_start_all(void)
{
    int count = thread_registry_count();
    int i;

    LOG_INFO("scheduler: starting %d threads", count);

    for (i = 0; i < count; i++)
    {
        const thread_entry_t *e = thread_registry_get(i);
        pthread_attr_t        attr;
        pthread_t             tid;

        pthread_attr_init(&attr);

        if (e->stack_size > 0U)
        {
            pthread_attr_setstacksize(&attr, e->stack_size);
        }

        if (e->sched_policy == SCHED_FIFO)
        {
            struct sched_param sp;
            sp.sched_priority = e->prio;
            pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
            pthread_attr_setschedparam(&attr, &sp);
            pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        }

        if (pthread_create(&tid, &attr, e->fn, e->arg) != 0)
        {
            LOG_ERROR("scheduler: failed to create thread [%s]", e->name);
            pthread_attr_destroy(&attr);
            return SW_ERR_HW;
        }

        pthread_detach(tid);
        pthread_attr_destroy(&attr);

        LOG_INFO("scheduler: started [%s]", e->name);
    }

    LOG_INFO("scheduler: all threads started");
    return SW_OK;
}
