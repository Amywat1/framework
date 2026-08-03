/**
 * @file    estop_poll_thread.h
 * @brief   急停轮询采集线程（可选入站适配器）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    定位：这是"如何采集急停"的一种实现，属于入站适配器而非运行时核心。
 *          它以 SCHED_FIFO 高优先级轮询安全端口的 estop_is_active()，在边沿处
 *          调用 cutout 并发布 EVT_HW_ESTOP_ON/OFF，不直接访问 OperationalMode。
 *
 * @note    是否接入由项目决定：若项目已有自己的急停采集通路（例如经 DI detector
 *          采样后由报警链路发布同样的事件），则不必链接本文件——重复采集只会
 *          让同一物理输入产生两条并发的事件源。M8 即属于后者。
 */

#ifndef ADAPTERS_INBOUND_SAFETY_ESTOP_POLL_THREAD_H
#define ADAPTERS_INBOUND_SAFETY_ESTOP_POLL_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注册急停轮询线程到线程表（须在 scheduler_start_all 之前调用）
 * @retval SW_OK           已登记
 * @retval SW_ERR_OVERFLOW 线程表已满
 * @note   依赖安全端口已注册；未注册时轮询读到的急停状态恒为 false。
 */
sw_err_t estop_poll_thread_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_INBOUND_SAFETY_ESTOP_POLL_THREAD_H */
