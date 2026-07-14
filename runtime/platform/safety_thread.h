/**
 * @file    safety_thread.h
 * @brief   EStop 硬件快速通道线程接口
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    阶段二（§7.3）：SCHED_FIFO 轮询急停 DI，仅执行
 *          safety_cutout_execute() + 发布 EVT_HW_ESTOP_ON/OFF，
 *          不直接访问 OperationalMode。
 */

#ifndef RUNTIME_PLATFORM_SAFETY_THREAD_H
#define RUNTIME_PLATFORM_SAFETY_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注册 safety_thread 到线程表（须在 scheduler_start_all 之前调用）
 */
sw_err_t safety_thread_init(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_PLATFORM_SAFETY_THREAD_H */
