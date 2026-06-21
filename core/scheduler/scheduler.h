/**
 * @file    scheduler.h
 * @brief   线程调度器接口（统一 pthread 创建）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef CORE_SCHEDULER_SCHEDULER_H
#define CORE_SCHEDULER_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  创建线程注册表中所有已注册的线程
 *         需在所有模块 init() 完成（线程已注册）后调用。
 * @retval SW_OK / SW_ERR_HW（pthread_create 失败）
 */
sw_err_t scheduler_start_all(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_SCHEDULER_SCHEDULER_H */
