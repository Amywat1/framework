/**
 * @file    hal_motion_port.h
 * @brief   运动控制 HAL 端口接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    供 domain 与 application 控制龙门、刷子使用，
 *          不直接依赖具体驱动或 SDK。
 */

#ifndef PORTS_HAL_MOTION_PORT_H
#define PORTS_HAL_MOTION_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 刷子选择（接触器语义，不含具体引脚）
 * ------------------------------------------------------------------------- */
typedef enum
{
    HAL_BRUSH_TOP  = 0, /* 顶刷（VFD 通过接触器1接入顶刷）*/
    HAL_BRUSH_SIDE = 1, /* 侧刷（VFD 通过接触器2接入侧刷）*/
} hal_brush_sel_t;

/* -------------------------------------------------------------------------
 * 运动控制操作表
 * ------------------------------------------------------------------------- */
typedef struct
{
    /* 龙门 VFD */

    /** @brief 龙门前进 @param freq_hz 频率（单位 0.01Hz，如 2500 = 25.00Hz）*/
    sw_err_t (*gantry_fwd)(uint16_t freq_hz);

    /** @brief 龙门后退 */
    sw_err_t (*gantry_rev)(uint16_t freq_hz);

    /** @brief 龙门停止 */
    sw_err_t (*gantry_stop)(void);

    /** @brief 龙门 VFD 故障复位（输出 200ms 复位脉冲）*/
    sw_err_t (*gantry_fault_reset)(void);

    /* 刷子 VFD */

    /**
     * @brief  切换刷子接触器
     * @note   调用前必须先停 VFD（hal_brush_stop），内部等待 200ms 后再吸合目标接触器
     */
    sw_err_t (*brush_select)(hal_brush_sel_t sel);

    /** @brief 刷子 VFD 启动（仅正转）@param freq_hz 频率（0.01Hz）*/
    sw_err_t (*brush_run)(uint16_t freq_hz);

    /** @brief 刷子 VFD 停止 */
    sw_err_t (*brush_stop)(void);

    /** @brief 刷子 VFD 故障复位 */
    sw_err_t (*brush_fault_reset)(void);
} hal_motion_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取实现（由 core/bootstrap/wiring.c 调用）
 * ------------------------------------------------------------------------- */

/**
 * @brief  注册运动控制实现（真机或仿真）
 * @param  ops  实现表指针，必须在 bootstrap 阶段调用，运行期不可更换
 */
void hal_motion_register(const hal_motion_ops_t *ops);

/**
 * @brief  获取当前注册的实现表（domain/device/ 内部使用）
 * @retval 已注册的 ops 指针；若未注册则触发 assert
 */
const hal_motion_ops_t *hal_motion_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_MOTION_PORT_H */
