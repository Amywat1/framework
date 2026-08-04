/**
 * @file    thread_registry.h
 * @brief   通用框架内部线程注册表接口（统一管理所有线程参数）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    通用框架内部线程在 init() 阶段调用 thread_register() 登记；
 *          bootstrap 最后调用 scheduler_start_all() 统一创建。
 */

#ifndef RUNTIME_SCHEDULER_THREAD_REGISTRY_H
#define RUNTIME_SCHEDULER_THREAD_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <pthread.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * 线程表最大容量
 *
 * 槽位构成（M8 实测，其他项目按同样口径核算）：
 *   框架固定占用      1  event_dispatch（急停轮询适配器如接入再加 1）
 *   引擎会话 worker   1~2 每个 engine_session 配置一个
 *   周期任务          9  见 periodic_task.c，与本上限共用同一容量
 * 合计约 13，留出余量以便项目新增周期任务时不必同步改框架常量。
 *
 * 提升此值会等量提升 PERIODIC_TASK_MAX（periodic_task.c 以此为准），
 * 每个槽位仅占用一个 thread_entry_t，不预留栈空间。
 * ------------------------------------------------------------------------- */
#define THREAD_REGISTRY_MAX 24

/* -------------------------------------------------------------------------
 * 线程描述条目
 * ------------------------------------------------------------------------- */
typedef struct {
    const char *name;    /* 线程名称（调试用）*/
    void *(*fn)(void *); /* 线程入口函数 */
    void  *arg;          /* 线程入口参数 */
    int    sched_policy; /* SCHED_OTHER 或 SCHED_FIFO */
    int    prio;         /* SCHED_FIFO 优先级（1~99）；SCHED_OTHER 保留传 0 */
    size_t stack_size;   /* 栈大小（字节）*/
} thread_entry_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  注册一个线程到全局线程表
 * @retval SW_OK / SW_ERR_OVERFLOW（超出 THREAD_REGISTRY_MAX）
 */
sw_err_t thread_register(const char *name, void *(*fn)(void *), int sched_policy, int prio, size_t stack_size);

/**
 * @brief  注册一个带入口参数的线程到全局线程表
 * @retval SW_OK / SW_ERR_PARAM / SW_ERR_OVERFLOW
 */
sw_err_t thread_register_arg(const char *name,
                             void *(*fn)(void *),
                             void  *arg,
                             int    sched_policy,
                             int    prio,
                             size_t stack_size);

/**
 * @brief  获取已注册线程总数
 */
int thread_registry_count(void);

/**
 * @brief  清空线程注册表（仅供单元测试消除用例间残留）
 * @note   生产路径不得调用：线程一经 scheduler_start_all 创建即以
 *         pthread_detach 运行且无法回收，清空登记表不会停止已启动的线程，
 *         只会让后续注册重新从 0 号槽开始，造成登记与实际线程不一致。
 *         周期任务槽由 periodic_task 自行管理，本函数不影响其占用。
 */
void thread_registry_reset_for_test(void);

/**
 * @brief  按下标获取线程条目（只读）
 */
const thread_entry_t *thread_registry_get(int idx);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_SCHEDULER_THREAD_REGISTRY_H */
