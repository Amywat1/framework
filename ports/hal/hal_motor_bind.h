/**
 * @file    hal_motor_bind.h
 * @brief   电机 HAL 绑定配置（机型适配层 → HAL 实现）
 * @author  HUWANGWEI
 * @date    2026-06-15
 */

#ifndef PORTS_HAL_HAL_MOTOR_BIND_H
#define PORTS_HAL_HAL_MOTOR_BIND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/io_handle.h"
#include <stdbool.h>

#define HAL_MOTOR_BIND_SLOT_MAX        8
/** @brief VFD 后端未绑定时使用（仅 HAL_MOTOR_DRV_DO 或占位） */
#define HAL_MOTOR_VFD_BACKEND_NONE     (-1)

typedef enum
{
    HAL_MOTOR_DRV_VFD = 0,
    HAL_MOTOR_DRV_DO,
} hal_motor_drv_type_t;

typedef struct
{
    hal_motor_drv_type_t drv_type;

    /**
     * @brief  VFD 后端实例 id（HAL_MOTOR_DRV_VFD 时必填）
     * @note   取值由机型 setup 填入，对应 hal_vfd_port 实例标识
     */
    int         vfd_backend_id;

    io_do_t     io_cw;
    io_do_t     io_ccw;
    io_do_t     io_stop;
    io_do_t     io_vel0;
    io_do_t     io_vel1;

    io_di_t     limit_io_cw;
    io_di_t     limit_io_ccw;

    bool        has_encoder;
    io_di_t     encoder_io;
} hal_motor_bind_cfg_t;

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_HAL_MOTOR_BIND_H */
