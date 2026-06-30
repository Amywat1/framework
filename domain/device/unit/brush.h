/**
 * @file    brush.h
 * @brief   刷子设备接口（顶刷 / 侧刷 VFD 选择管理）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    顶刷与侧刷共享同一 VFD。接触器切换由 motor 层的 pre_start 回调
 *          非阻塞完成（在 motor_tick 线程中，VFD 停止延迟满足后执行）。
 *          brush 模块仅负责刷子 ID 选择与电机启停，无阻塞操作。
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
    BRUSH_ID_TOP  = 0, /**< 顶刷 */
    BRUSH_ID_SIDE = 1, /**< 侧刷 */
    BRUSH_ID_NONE = 0xFF,
} brush_id_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化刷子组件，重置内部状态
 */
sw_err_t brush_init(void);

/**
 * @brief  启动指定刷子
 * @note   非阻塞。若需要切换接触器，motor 层将在 tick 线程异步完成切换后再输出。
 * @param  id       目标刷子 ID（BRUSH_ID_TOP / BRUSH_ID_SIDE）
 * @param  freq_hz  VFD 频率（0.01Hz 单位，如 4500 = 45.00Hz）；传 0 等同于 brush_stop()
 */
sw_err_t brush_start(brush_id_t id, uint16_t freq_hz);

/**
 * @brief  停止刷子 VFD（保留当前接触器选择，供下次同刷子快速重启）
 */
sw_err_t brush_stop(void);

/**
 * @brief  停止刷子并重置 ID 选择状态（洗车结束后调用）
 */
sw_err_t brush_off(void);

/**
 * @brief  查询指定刷子是否正在运行
 */
bool brush_is_running(brush_id_t id);

/**
 * @brief  获取当前选中的刷子 ID（BRUSH_ID_NONE = 无）
 */
brush_id_t brush_get_active(void);

/**
 * @brief  查询刷子 VFD 是否处于故障态
 */
bool brush_is_fault(void);

/**
 * @brief  获取刷子 VFD 负载电流（motor_tick 周期采样，单位 0.1A；未运行时返回 0）
 */
uint16_t brush_get_current(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_BRUSH_H */
