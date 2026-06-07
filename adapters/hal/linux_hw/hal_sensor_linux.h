/**
 * @file    hal_sensor_linux.h
 * @brief   Linux 真机 DI 通道滤波 HAL（仅供 machine 层 bind）
 * @author  胡望伟
 * @date    2026-06-07
 */

#ifndef ADAPTERS_HAL_LINUX_HW_HAL_SENSOR_LINUX_H
#define ADAPTERS_HAL_LINUX_HW_HAL_SENSOR_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ports/hal/hal_sensor_port.h"
#include "common/sw_error.h"

/** @brief  注册 hal_sensor_linux 实现到 hal_sensor_port */
void hal_sensor_linux_register(void);

/**
 * @brief  绑定通道滤波参数
 * @param  ch   通道编号
 * @param  cfg  绑定配置；trig_count / release_count 须大于 0
 */
sw_err_t hal_sensor_linux_bind(hal_sensor_channel_t           ch,
                               const hal_sensor_bind_cfg_t   *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_LINUX_HW_HAL_SENSOR_LINUX_H */
