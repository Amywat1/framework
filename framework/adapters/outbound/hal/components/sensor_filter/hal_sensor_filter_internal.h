/**
 * @file    hal_sensor_filter_internal.h
 * @brief   DI 滤波组件内部运行接口
 * @author  HUWANGWEI
 * @date    2026-07-05
 *
 * @note    仅供 sensor_filter 组件内部 poll 任务和 framework 级测试使用；
 *          项目层不得依赖本文件创建运行期轮询线程。
 */

#ifndef ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_FILTER_INTERNAL_H
#define ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_FILTER_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  执行一轮 DI 滤波采样。
 * @retval SW_OK        采样完成。
 * @retval SW_ERR_NOT_INIT 依赖的 IO 后端未就绪。
 * @note   本接口是 sensor_filter 组件内部运行入口，不属于项目业务 API。
 */
sw_err_t hal_sensor_filter_tick_once(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_COMPONENTS_SENSOR_FILTER_HAL_SENSOR_FILTER_INTERNAL_H */
