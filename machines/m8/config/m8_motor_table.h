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
#include "machines/m8/config/m8_io_pins.h"
#include "machines/m8/config/m8_vfd_table.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    MOTOR_GANTRY = 0,
    MOTOR_ID_MAX
} motor_id_t;

typedef enum
{
    MOTOR_DRV_VFD = 0,
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
} motor_limit_mode_t;

typedef enum
{
    MOTOR_ENCODER_NONE = 0,
    MOTOR_ENCODER_COUNTER,
} motor_encoder_backend_t;

#define MOTOR_TIMEOUT_FOREVER   0xFFFFFFFFU
#define MOTOR_DO_NONE           ((io_do_t){IO_HANDLE_NULL})
#define MOTOR_DI_NONE           ((io_di_t){IO_HANDLE_NULL})

typedef struct
{
    int                  id;
    const char          *name;
    motor_drv_type_t     drv_type;
    motor_action_type_t  action_type;
    /**
     * @brief  HAL VFD 实例 id（MOTOR_DRV_VFD 时必填，否则填 HAL_VFD_ID_NONE）
     * @note   取值见 config/machine/m8_vfd_table.h 中 HAL_VFD_GANTRY / HAL_VFD_BRUSH
     */
    int                  vfd_id;

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

    bool                     has_encoder;
    motor_encoder_backend_t  encoder_backend;
    io_di_t              encoder_io;
    io_di_t              encoder_zero_io;
    uint8_t              encoder_zero_confirm;
    uint8_t              encoder_err_threshold;
    uint16_t             encoder_err_check_ms;
    uint16_t             encoder_jump_threshold;

    uint16_t             current_high_threshold;
    uint16_t             current_low_threshold;
    uint16_t             current_check_delay_ms;
    uint16_t             current_confirm_ms;
} motor_cfg_t;

static const motor_cfg_t m8_motor_table[] = {
    {
        .id              = MOTOR_GANTRY,
        .name            = "GANTRY",
        .drv_type        = MOTOR_DRV_VFD,
        .vfd_id          = HAL_VFD_GANTRY,
        .action_type     = MOTOR_ACTION_MOVE,
        .io_cw           = M8_IO_DO_GANTRY_FWD,
        .io_ccw          = M8_IO_DO_GANTRY_REV,
        .io_stop         = MOTOR_DO_NONE,
        .io_vel0         = MOTOR_DO_NONE,
        .io_vel1         = MOTOR_DO_NONE,
        .limit_mode      = MOTOR_LIMIT_SIGNAL,
        .limit_io_cw     = M8_IO_DI_GANTRY_FWD_LIMIT,
        .limit_io_ccw    = M8_IO_DI_GANTRY_REV_LIMIT,
        .limit_pos_min   = 0U,
        .limit_pos_max   = 0U,
        .timeout_ms      = 60000U,
        .has_encoder            = true,
        .encoder_backend        = MOTOR_ENCODER_COUNTER,
        .encoder_io             = M8_IO_DI_GANTRY_ENCODER_PULSE,
        .encoder_zero_io        = MOTOR_DI_NONE,
        .encoder_zero_confirm   = 0U,
        .encoder_err_threshold  = 5U,
        .encoder_err_check_ms   = 400U,
        .encoder_jump_threshold = 0U,
        .current_high_threshold = 0U,
        .current_low_threshold  = 0U,
        .current_check_delay_ms = 0U,
        .current_confirm_ms     = 0U,
    },
};

#define M8_MOTOR_TABLE_SIZE ((int)(sizeof(m8_motor_table) / sizeof(m8_motor_table[0])))

#endif /* CONFIG_MACHINE_M8_MOTOR_TABLE_H */
