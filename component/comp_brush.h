/**
 * @file    comp_brush.h
 * @brief   刷子组件接口（顶刷 + 侧刷，封装 VFD + 接触器切换逻辑）
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    M8 共 1 台 VFD 驱动 3 把刷子，接触器互锁切换：
 *          接触器1（H29）吸合 → 顶刷旋转
 *          接触器2（H28）吸合 → 侧刷旋转（左右同步）
 *          切换顺序：停转 → 断开当前接触器（200ms）→ 吸合目标接触器
 */

#ifndef COMP_BRUSH_H
#define COMP_BRUSH_H

#include "common/sw_types.h"
#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 刷子 ID
 * ------------------------------------------------------------------------- */
typedef enum
{
    BRUSH_ID_TOP  = 0,  /* 顶刷 */
    BRUSH_ID_SIDE = 1,  /* 侧刷（左右同控）*/
    BRUSH_ID_MAX
} BrushId_t;

/* -------------------------------------------------------------------------
 * 接口声明
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化刷子组件
 * @retval SW_OK
 */
sw_err_t comp_brush_init(void);

/**
 * @brief  启动指定刷子旋转
 * @param  id       刷子 ID
 * @param  freq_hz  VFD 目标频率（0.01Hz，如 4500=45.00Hz）
 * @retval SW_OK / SW_ERR_STATE（切换接触器时 VFD 未停止）/ SW_ERR_COMM
 * @note   若当前运行的是不同刷子，函数内部会先停转、切换接触器，再启动
 */
sw_err_t comp_brush_start(BrushId_t id, uint16_t freq_hz);

/**
 * @brief  停止所有刷子旋转（VFD 减速停车，不断开接触器）
 * @retval SW_OK
 */
sw_err_t comp_brush_stop(void);

/**
 * @brief  停止旋转并断开所有接触器（洗车完成时调用）
 * @retval SW_OK
 */
sw_err_t comp_brush_off(void);

/**
 * @brief  查询指定刷子当前是否在运转
 * @retval true=运转中
 */
bool comp_brush_is_running(BrushId_t id);

/**
 * @brief  查询当前哪把刷子接入 VFD
 * @retval BrushId_t / BRUSH_ID_MAX（无接入）
 */
BrushId_t comp_brush_get_active(void);

#endif /* COMP_BRUSH_H */
