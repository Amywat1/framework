/**
 * @file    hal_sensor_poll.h
 * @brief   HAL 传感器滤波周期任务注册
 * @author  HUWANGWEI
 * @date    2026-07-04
 *
 * @note    仅驱动通用 hal_sensor 滤波采样，不含任何项目点位/信号表语义。
 */

#ifndef ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_POLL_H
#define ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_POLL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  注册 HAL 传感器滤波周期任务。
 * @retval SW_OK 注册成功。
 * @retval SW_ERR_PARAM / SW_ERR_OVERFLOW 注册失败。
 * @note   任务由 scheduler_start_all() 统一启动。
 */
sw_err_t hal_sensor_poll_register_task(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_POLL_H */
