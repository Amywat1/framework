/**
 * @file    hal_sensor_filter.h
 * @brief   DI 通道滤波 HAL 内部接口（仅供机型适配层绑定通道�?
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    仅供 projects/<project>/wiring/ �?bootstrap 阶段调用�?
 *          业务层仍通过 hal_sensor_port 访问�?
 *          本文件不含任何平台专�?SDK 依赖，可用于任何已注�?
 *          hal_io_port 的目标平台�?
 */

#ifndef ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_FILTER_H
#define ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/ports/outbound/hal/hal_sensor_port.h"
#include "framework/common/sw_error.h"

/** @brief  注册通用 DI 滤波 HAL 实现�?hal_sensor_port */
void hal_sensor_filter_register(void);

/**
 * @brief  绑定通道滤波参数
 * @param  ch   通道编号
 * @param  cfg  绑定配置；trig_count / release_count 须大�?0
 */
sw_err_t hal_sensor_bind(hal_sensor_channel_t         ch,
                         const hal_sensor_bind_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_FILTER_H */
