/**
 * @file    report_scheduler.h
 * @brief   云端状态上报调度器接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef APPLICATION_ORCHESTRATORS_REPORT_SCHEDULER_H
#define APPLICATION_ORCHESTRATORS_REPORT_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdint.h>

/**
 * @brief  启动云端上报：单一周期任务，同时 poll 链路、watcher 与上报。
 *
 * @param  poll_ms        poll 周期，必须大于 0。
 * @param  full_period_ms 全量上报周期；0 表示只在重连时全量。
 * @retval SW_OK 启动成功；已启动时再次调用也返回 SW_OK。
 * @retval SW_ERR_PARAM 周期非法，或 full_period_ms 非 0 且不是 poll_ms 的整数倍。
 * @retval SW_ERR_NOT_INIT 物模型尚未注册。
 * @note   后台线程由 scheduler 统一启动。本函数会初始化 watcher 并订阅
 *         EVT_CLOUD_CONNECTED 做全量重同步。
 */
sw_err_t report_scheduler_start(uint32_t poll_ms, uint32_t full_period_ms);

/**
 * @brief  执行一拍 poll / 脏点增量 / 到期全量（周期任务回调与单测共用）
 */
void report_scheduler_poll(void);

/**
 * @brief  清空调度器状态（仅供单元测试）
 */
void report_scheduler_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_REPORT_SCHEDULER_H */
