/**
 * @file    thread_registry.h
 * @brief   线程注册表接口（统一管理所有线程参数）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    应用编排线程在 init() 阶段调用 thread_register() 登记；
 *          bootstrap 最后调用 scheduler_start_all() 统一创建。
 *          IO 读写后台线程（drv_io）由驱动模块自行管理，不在此注册。
 */

#ifndef CORE_SCHEDULER_THREAD_REGISTRY_H
#define CORE_SCHEDULER_THREAD_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <pthread.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * 线程表最大容量
 * ------------------------------------------------------------------------- */
#define THREAD_REGISTRY_MAX     8

/* -------------------------------------------------------------------------
 * 线程描述条目
 * ------------------------------------------------------------------------- */
typedef struct
{
    const char    *name;          /* 线程名称（调试用）*/
    void         *(*fn)(void *);  /* 线程入口函数 */
    int            sched_policy;  /* SCHED_OTHER 或 SCHED_FIFO */
    int            prio;          /* SCHED_FIFO 优先级（1~99）或 SCHED_OTHER nice 值 */
    size_t         stack_size;    /* 栈大小（字节）*/
} thread_entry_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  注册一个线程到全局线程表
 * @retval SW_OK / SW_ERR_OVERFLOW（超出 THREAD_REGISTRY_MAX）
 */
sw_err_t thread_register(const char *name, void *(*fn)(void *),
                         int sched_policy, int prio, size_t stack_size);

/**
 * @brief  获取已注册线程总数
 */
int thread_registry_count(void);

/**
 * @brief  按下标获取线程条目（只读）
 */
const thread_entry_t *thread_registry_get(int idx);

#ifdef __cplusplus
}
#endif

#endif /* CORE_SCHEDULER_THREAD_REGISTRY_H */
