/**
 * @file    m8_motor_table.h
 * @brief   M8 电机配置表（编译期只读）
 * @author  HUWANGWEI
 * @date    2026-04-13
 *
 * @note    当前电机管理层统一从本表加载配置。
 *          后续新增电机时，优先在这里补充一条配置，再扩展语义层封装。
 */

#ifndef CONFIG_MACHINE_M8_MOTOR_TABLE_H
#define CONFIG_MACHINE_M8_MOTOR_TABLE_H

#include "common/io_handle.h"
#include "driver/drv_io.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    MOTOR_GANTRY = 0,
    MOTOR_BRUSH_TOP,
    MOTOR_BRUSH_SIDE,
    MOTOR_ID_MAX
} motor_id_t;

typedef enum
{
    MOTOR_DRV_VFD = 0,
    MOTOR_DRV_KM,
    MOTOR_DRV_PULSE,
} motor_drv_type_t;

typedef enum
{
    MOTOR_ACTION_HOLD = 0,
    MOTOR_ACTION_MOVE,
} motor_action_type_t;

typedef enum
{
    MOTOR_LIMIT_NONE = 0,
    MOTOR_LIMIT_SIGNAL,
    MOTOR_LIMIT_PULSE_MAX,
    MOTOR_LIMIT_PULSE_MIN_MAX,
} motor_limit_mode_t;

#define MOTOR_TIMEOUT_FOREVER   0xFFFFFFFFU
#define MOTOR_DO_NONE           ((io_do_t){IO_HANDLE_NULL})
#define MOTOR_DI_NONE           ((io_di_t){IO_HANDLE_NULL})

typedef struct
{
    int                  id;
    const char          *name;
    motor_drv_type_t     drv_type;
    motor_action_type_t  action_type;

    io_do_t              io_cw;
    io_do_t              io_ccw;
    io_do_t              io_stop;
    io_do_t              io_vel0;
    io_do_t              io_vel1;

    motor_limit_mode_t   limit_mode;
    io_di_t              limit_io_cw;
    io_di_t              limit_io_ccw;
    uint16_t             limit_pos_min;
    uint16_t             limit_pos_max;

    uint32_t             timeout_ms;

    bool                 has_encoder;
    io_di_t              encoder_io;
    io_di_t              encoder_zero_io;
} motor_cfg_t;

static const motor_cfg_t m8_motor_table[] = {
    {
        .id              = MOTOR_GANTRY,
        .name            = "GANTRY",
        .drv_type        = MOTOR_DRV_VFD,
        .action_type     = MOTOR_ACTION_MOVE,
        .io_cw           = DO_GANTRY_FWD,
        .io_ccw          = DO_GANTRY_REV,
        .io_stop         = MOTOR_DO_NONE,
        .io_vel0         = MOTOR_DO_NONE,
        .io_vel1         = MOTOR_DO_NONE,
        .limit_mode      = MOTOR_LIMIT_SIGNAL,
        .limit_io_cw     = DI_GANTRY_FWD_LIMIT,
        .limit_io_ccw    = DI_GANTRY_REAR_LIMIT,
        .limit_pos_min   = 0U,
        .limit_pos_max   = 0U,
        .timeout_ms      = 60000U,
        .has_encoder     = true,
        .encoder_io      = DI_ENCODER_PULSE,
        .encoder_zero_io = MOTOR_DI_NONE,
    },
    {
        .id              = MOTOR_BRUSH_TOP,
        .name            = "BRUSH_TOP",
        .drv_type        = MOTOR_DRV_VFD,
        .action_type     = MOTOR_ACTION_HOLD,
        .io_cw           = DO_SIDE_BRUSH_FWD,
        .io_ccw          = DO_SIDE_BRUSH_REV,
        .io_stop         = MOTOR_DO_NONE,
        .io_vel0         = MOTOR_DO_NONE,
        .io_vel1         = MOTOR_DO_NONE,
        .limit_mode      = MOTOR_LIMIT_NONE,
        .limit_io_cw     = MOTOR_DI_NONE,
        .limit_io_ccw    = MOTOR_DI_NONE,
        .limit_pos_min   = 0U,
        .limit_pos_max   = 0U,
        .timeout_ms      = MOTOR_TIMEOUT_FOREVER,
        .has_encoder     = false,
        .encoder_io      = MOTOR_DI_NONE,
        .encoder_zero_io = MOTOR_DI_NONE,
    },
    {
        .id              = MOTOR_BRUSH_SIDE,
        .name            = "BRUSH_SIDE",
        .drv_type        = MOTOR_DRV_VFD,
        .action_type     = MOTOR_ACTION_HOLD,
        .io_cw           = DO_SIDE_BRUSH_FWD,
        .io_ccw          = DO_SIDE_BRUSH_REV,
        .io_stop         = MOTOR_DO_NONE,
        .io_vel0         = MOTOR_DO_NONE,
        .io_vel1         = MOTOR_DO_NONE,
        .limit_mode      = MOTOR_LIMIT_NONE,
        .limit_io_cw     = MOTOR_DI_NONE,
        .limit_io_ccw    = MOTOR_DI_NONE,
        .limit_pos_min   = 0U,
        .limit_pos_max   = 0U,
        .timeout_ms      = MOTOR_TIMEOUT_FOREVER,
        .has_encoder     = false,
        .encoder_io      = MOTOR_DI_NONE,
        .encoder_zero_io = MOTOR_DI_NONE,
    },
};

#define M8_MOTOR_TABLE_SIZE ((int)(sizeof(m8_motor_table) / sizeof(m8_motor_table[0])))

#endif /* CONFIG_MACHINE_M8_MOTOR_TABLE_H */
