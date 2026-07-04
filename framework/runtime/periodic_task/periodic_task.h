/**
 * @file    periodic_task.h
 * @brief   Runtime periodic task registration helper.
 */

#ifndef FRAMEWORK_RUNTIME_PERIODIC_TASK_H
#define FRAMEWORK_RUNTIME_PERIODIC_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"
#include <stddef.h>
#include <stdint.h>

typedef void (*periodic_task_fn_t)(void *ctx);

/**
 * @brief Register a detached periodic task through the runtime scheduler.
 *
 * The task starts when scheduler_start_all() is called. It runs forever using
 * a fixed sleep period and has no stop semantics, matching current scheduler
 * lifecycle behavior.
 */
sw_err_t periodic_task_register(const char      *name,
                                uint32_t         period_ms,
                                periodic_task_fn_t fn,
                                void            *ctx,
                                int              sched_policy,
                                int              prio,
                                size_t           stack_size);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_RUNTIME_PERIODIC_TASK_H */
