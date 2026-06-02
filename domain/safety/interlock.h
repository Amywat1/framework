/**
 * @file    interlock.h
 * @brief   运动互锁条件检查接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    在执行任何运动指令前调用 interlock_check_motion()。
 *          硬件层面的互锁（如接触器切换前停 VFD）在 HAL 层实现；
 *          此处为软件层面的业务互锁（如报警激活时禁止运动）。
 */

#ifndef DOMAIN_SAFETY_INTERLOCK_H
#define DOMAIN_SAFETY_INTERLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 运动类型（用于互锁语义区分）
 * ------------------------------------------------------------------------- */
typedef enum
{
    MOTION_TYPE_GANTRY_FWD   = 0, /* 龙门前进 */
    MOTION_TYPE_GANTRY_REV,       /* 龙门后退 */
    MOTION_TYPE_BRUSH_SWITCH,     /* 刷子切换（接触器切换）*/
    MOTION_TYPE_LIFT_UP,          /* 顶刷上升 */
    MOTION_TYPE_LIFT_DOWN,        /* 顶刷下降 */
} motion_type_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  运动前互锁检查
 * @param  type  要执行的运动类型
 * @retval SW_OK         可以执行
 * @retval SW_ERR_STATE  互锁条件不满足（如报警激活）
 */
sw_err_t interlock_check_motion(motion_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_INTERLOCK_H */
