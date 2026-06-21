/**
 * @file    brush.h
 * @brief   刷子设备接口（顶刷 / 侧刷 VFD 及接触器管理）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    当前未接入硬件控制；brush_start() 返回 SW_ERR_NOT_SUPPORT。
 */

#ifndef DOMAIN_DEVICE_BRUSH_H
#define DOMAIN_DEVICE_BRUSH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 刷子 ID
 * ------------------------------------------------------------------------- */
typedef enum
{
    BRUSH_ID_TOP  = 0, /* 顶刷（H29 接触器）*/
    BRUSH_ID_SIDE = 1, /* 侧刷（H28 接触器）*/
    BRUSH_ID_NONE = 0xFF,
} brush_id_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化刷子组件（清零内部状态）
 */
sw_err_t brush_init(void);

/**
 * @brief  启动指定刷子
 *         若当前运行的是不同刷子，先停 VFD 再切换接触器（由 HAL 实现互锁）。
 *         成功后发布 EVT_COMP_BRUSH_STARTED。
 * @param  id       目标刷子 ID
 * @param  freq_hz  VFD 频率（0.01Hz，如 4500 = 45.00Hz）
 */
sw_err_t brush_start(brush_id_t id, uint16_t freq_hz);

/**
 * @brief  停止刷子 VFD（不断接触器）
 */
sw_err_t brush_stop(void);

/**
 * @brief  停止刷子并重置接触器选择状态（洗车结束后调用）
 */
sw_err_t brush_off(void);

/**
 * @brief  查询指定刷子是否正在运行
 */
bool brush_is_running(brush_id_t id);

/**
 * @brief  获取当前接入 VFD 的刷子 ID（BRUSH_ID_NONE = 无）
 */
brush_id_t brush_get_active(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_BRUSH_H */
