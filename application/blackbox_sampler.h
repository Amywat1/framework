/**
 * @file    blackbox_sampler.h
 * @brief   可选默认通道：周期采样 + 显式触发表
 *
 * @note    未 start 时 tick / 触发均为空操作。不要和项目自己的 record 叠在同一实例上。
 */

#ifndef APPLICATION_BLACKBOX_SAMPLER_H
#define APPLICATION_BLACKBOX_SAMPLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdint.h>

/** 默认采样频率（Hz） */
#define BLACKBOX_SAMPLER_HZ 10U

#define BLACKBOX_TRIG_ESTOP    (1U << 0) /**< 急停边沿 */
#define BLACKBOX_TRIG_LOCKOUT  (1U << 1) /**< LOCKOUT 姿态 */
#define BLACKBOX_TRIG_WATCHDOG (1U << 2) /**< 看门狗，须项目在绊索里调用 on_watchdog_trip */

/**
 * @brief  初始化黑匣子、登记采样任务，并勾选触发源
 * @param  pre_seconds   触发前窗口，须大于 0
 * @param  post_seconds  触发后窗口
 * @param  triggers      BLACKBOX_TRIG_* 按位或；0 表示只采样不自动冻
 * @retval SW_OK           已就绪；已启动过则保持原配置，不再订阅或登记
 * @retval SW_ERR_PARAM    窗口非法
 * @retval SW_ERR_NOT_INIT 勾选了事件源但总线未就绪
 */
sw_err_t blackbox_sampler_start(uint16_t pre_seconds, uint16_t post_seconds, uint32_t triggers);

/**
 * @brief  写入一拍快照精简包
 */
void blackbox_sampler_tick(void);

/**
 * @brief  看门狗绊索入口；未勾选 WATCHDOG 时忽略
 */
void blackbox_sampler_on_watchdog_trip(void);

/**
 * @brief  测试复位触发表
 */
void blackbox_sampler_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_BLACKBOX_SAMPLER_H */
