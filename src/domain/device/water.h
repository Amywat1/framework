/**
 * @file    water.h
 * @brief   水路设备接口（泵 / 水帘 / 泡沫 / 刷子冲水 / 高压）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    通过 hal_water_port 接口控制，同步接口（水阀为瞬时开关）。
 */

#ifndef DOMAIN_DEVICE_WATER_H
#define DOMAIN_DEVICE_WATER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化水路组件（关闭所有水路）
 */
sw_err_t water_init(void);

/**
 * @brief  开启预洗（水泵 + 泡沫 + 水帘）
 */
sw_err_t water_prewash_on(void);

/**
 * @brief  关闭预洗（泡沫 + 水帘，水泵保留给后续步骤）
 */
sw_err_t water_prewash_off(void);

/**
 * @brief  开启刷子冲水（水泵 + 刷子水阀）
 */
sw_err_t water_brush_on(void);

/**
 * @brief  关闭刷子冲水
 */
sw_err_t water_brush_off(void);

/**
 * @brief  开启高压冲洗（水泵 + 高压阀）
 */
sw_err_t water_highpres_on(void);

/**
 * @brief  关闭高压冲洗
 */
sw_err_t water_highpres_off(void);

/**
 * @brief  关闭所有水路（水泵 + 全部水阀）
 */
sw_err_t water_all_off(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_WATER_H */
