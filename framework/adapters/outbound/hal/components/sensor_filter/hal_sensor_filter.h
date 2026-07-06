/**
 * @file    hal_sensor_filter.h
 * @brief   DI 通道滤波 HAL 通用适配层注册接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    仅用于注册通用 DI 滤波实现到 hal_sensor_port；
 *          通道绑定请使用 hal_sensor_port.h 中 hal_sensor_ops_t.bind()。
 *          本文件不含任何平台专属 SDK 依赖，可用于任何已注册
 *          hal_io_port 的目标平台。
 */

#ifndef ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_FILTER_H
#define ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/ports/outbound/hal/hal_sensor_port.h"

/** @brief  注册通用 DI 滤波 HAL 实现到 hal_sensor_port */
void hal_sensor_filter_register(void);

/**
 * @brief  注册 HAL 传感器滤波周期任务。
 * @retval SW_OK 注册成功。
 * @retval SW_ERR_PARAM / SW_ERR_OVERFLOW 注册失败。
 * @note   任务由 scheduler_start_all() 统一启动；项目层只注册任务，不直接调用 tick。
 */
sw_err_t hal_sensor_poll_register_task(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_FILTER_H */
