/**
 * @file    comp_water.h
 * @brief   水路组件接口（封装所有水阀和水泵控制）
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef COMP_WATER_H
#define COMP_WATER_H

#include "common/sw_types.h"
#include "common/sw_error.h"

/**
 * @brief  初始化水路组件（上电关闭所有阀门和水泵）
 */
sw_err_t comp_water_init(void);

/**
 * @brief  启动预洗（水泵 + 泡沫阀 + 清水水帘）
 */
sw_err_t comp_water_prewash_on(void);

/**
 * @brief  停止预洗
 */
sw_err_t comp_water_prewash_off(void);

/**
 * @brief  启动侧刷冲水
 */
sw_err_t comp_water_brush_on(void);

/**
 * @brief  停止侧刷冲水
 */
sw_err_t comp_water_brush_off(void);

/**
 * @brief  启动高压冲洗
 */
sw_err_t comp_water_highpres_on(void);

/**
 * @brief  停止高压冲洗
 */
sw_err_t comp_water_highpres_off(void);

/**
 * @brief  关闭所有水路（紧急停机时调用）
 */
sw_err_t comp_water_all_off(void);

#endif /* COMP_WATER_H */
