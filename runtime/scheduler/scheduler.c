/**
 * @file    scheduler.c
 * @brief   线程调度器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

/* pthread_setname_np 是 glibc 扩展，需在任何头文件之前开启 _GNU_SOURCE。
 * 仅本文件需要，故就地 define 而不在 CMakeLists 全局加编译选项。 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "runtime/scheduler/scheduler.h"

#include "common/log.h"
#include "runtime/scheduler/thread_registry.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <sys/resource.h>

/* Linux 线程名上限 16 字节（含结尾 '\0'）*/
#define SCHED_OS_THREAD_NAME_MAX 16

sw_err_t scheduler_start_all(void)
{
    int count = thread_registry_count();
    int i;

    LOG_INFO("scheduler: starting %d threads", count);

    for (i = 0; i < count; i++) {
        const thread_entry_t *e = thread_registry_get(i);
        pthread_attr_t        attr;
        pthread_t             tid;

        pthread_attr_init(&attr);

        if (e->stack_size > 0U) {
            pthread_attr_setstacksize(&attr, e->stack_size);
        }

        if (e->sched_policy == SCHED_FIFO) {
            struct sched_param sp;
            sp.sched_priority = e->prio;
            pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
            pthread_attr_setschedparam(&attr, &sp);
            pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        }

        if (pthread_create(&tid, &attr, e->fn, e->arg) != 0) {
            if (e->sched_policy == SCHED_FIFO) {
                LOG_WARN("scheduler: SCHED_FIFO create failed for [%s], fallback SCHED_OTHER", e->name);
                pthread_attr_destroy(&attr);
                pthread_attr_init(&attr);
                if (e->stack_size > 0U) {
                    pthread_attr_setstacksize(&attr, e->stack_size);
                }
                if (pthread_create(&tid, &attr, e->fn, e->arg) != 0) {
                    LOG_ERROR("scheduler: failed to create thread [%s]", e->name);
                    pthread_attr_destroy(&attr);
                    return SW_ERR_HW;
                }
            } else {
                LOG_ERROR("scheduler: failed to create thread [%s]", e->name);
                pthread_attr_destroy(&attr);
                return SW_ERR_HW;
            }
        }

        /* 同步 OS 层线程名，便于 top/gdb/perf 与日志 sink 区分线程。
         * 超长必须先截断，否则 pthread_setname_np 直接返回 ERANGE 而不做任何设置。
         * 线程名纯属可观测性辅助，设置失败降级为 WARN 继续启动。 */
        {
            char os_name[SCHED_OS_THREAD_NAME_MAX];

            (void)snprintf(os_name, sizeof(os_name), "%s", e->name);
            if (pthread_setname_np(tid, os_name) != 0) {
                LOG_WARN("scheduler: setname failed for [%s]", e->name);
            }
        }

        pthread_detach(tid);
        pthread_attr_destroy(&attr);

        LOG_INFO("scheduler: started [%s]", e->name);
    }

    LOG_INFO("scheduler: all threads started");
    return SW_OK;
}
