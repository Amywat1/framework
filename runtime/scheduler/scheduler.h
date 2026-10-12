/**
 * @file    scheduler.h
 * @brief   线程调度器接口（通用框架内部线程统一 pthread 创建）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef RUNTIME_SCHEDULER_SCHEDULER_H
#define RUNTIME_SCHEDULER_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  创建线程注册表中所有已注册的线程
 *         需在所有模块 init() 完成（线程已注册）后调用。
 * @retval SW_OK / SW_ERR_HW（pthread_create 失败）
 * @note   `SCHED_FIFO` 创建失败时：未定义 `WDF_ALLOW_SCHED_FIFO_FALLBACK=0`
 *         则降级为 `SCHED_OTHER` 并继续启动；真机目标应禁止降级，使急停
 *         轮询与控制环拿不到实时优先级时直接启动失败。
 */
sw_err_t scheduler_start_all(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_SCHEDULER_SCHEDULER_H */
