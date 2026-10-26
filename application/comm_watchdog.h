/**
 * @file    comm_watchdog.h
 * @brief   周期通讯心跳轮询器：超时出报警，恢复消报警
 *
 * @note    可选。设备清单、超时和报警码由项目登记。未 bind 则 poll 不做事。
 *          丢失上升沿可回调，CRITICAL 切断等副作用留项目。
 */

#ifndef APPLICATION_COMM_WATCHDOG_H
#define APPLICATION_COMM_WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdint.h>

/** @brief 单机最多监控通道 */
#define COMM_WATCHDOG_CHANNEL_MAX 16U

/**
 * @brief  一条心跳通道
 */
typedef struct {
    uint32_t timeout_ms; /**< 超过该间隔未心跳则视为丢失；须大于 0 */
    uint32_t alarm_code; /**< 项目报警码 */
} comm_watchdog_channel_t;

/**
 * @brief  通道刚丢失时回调（每个通道只在上升沿调一次）
 * @param  id 通道下标，0 .. count-1
 */
typedef void (*comm_watchdog_on_lost_fn_t)(unsigned id);

/**
 * @brief  登记通道表（只存指针，表须静态或与模块同寿）
 * @param  channels 通道行
 * @param  count    行数，1..COMM_WATCHDOG_CHANNEL_MAX
 * @param  on_lost  丢失上升沿；可为 NULL
 * @retval SW_OK           已登记
 * @retval SW_ERR_PARAM    表空或 timeout_ms 为 0
 * @retval SW_ERR_OVERFLOW 超过 COMM_WATCHDOG_CHANNEL_MAX
 */
sw_err_t comm_watchdog_bind(const comm_watchdog_channel_t *channels, unsigned count,
                            comm_watchdog_on_lost_fn_t on_lost);

/**
 * @brief  把各通道心跳打到当前时刻，给首次通讯留满超时窗
 */
void comm_watchdog_init(void);

/**
 * @brief  更新一条通道的心跳时刻
 * @param  id 通道下标；越界则忽略
 */
void comm_watchdog_heartbeat(unsigned id);

/**
 * @brief  检查超时：丢失则 trigger，恢复则 clear
 * @note   未注册 alarm_binding 时直接返回。丢失期间每拍都 trigger。
 */
void comm_watchdog_poll(void);

/**
 * @brief  清空模块状态，仅供测试隔离
 */
void comm_watchdog_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_COMM_WATCHDOG_H */
