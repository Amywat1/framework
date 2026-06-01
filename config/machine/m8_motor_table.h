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
#include "domain/model/alarm_code.h"
#include "adapters/hal/linux_hw/drv/drv_io.h"
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
    uint16_t             alarm_code_current;
    uint16_t             alarm_code_fault;
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
        .limit_io_ccw    = DI_GANTRY_REV_LIMIT,
        .limit_pos_min   = 0U,
        .limit_pos_max   = 0U,
        .timeout_ms      = 60000U,
        .has_encoder            = true,
        .encoder_backend        = MOTOR_ENCODER_COUNTER,
        .encoder_io             = DI_GANTRY_ENCODER_PULSE,
        .encoder_zero_io        = MOTOR_DI_NONE,
        .encoder_zero_confirm   = 0U,
        .encoder_err_threshold  = 5U,
        .encoder_err_check_ms   = 400U,
        .encoder_jump_threshold = 0U,
        .current_high_threshold = 0U,
        .current_low_threshold  = 0U,
        .current_check_delay_ms = 0U,
        .current_confirm_ms     = 0U,
        .alarm_code_current     = ALARM_CODE_GANTRY_CURRENT,
        .alarm_code_fault       = ALARM_CODE_VFD_GANTRY,
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
        .has_encoder            = false,
        .encoder_backend        = MOTOR_ENCODER_NONE,
        .encoder_io             = MOTOR_DI_NONE,
        .encoder_zero_io        = MOTOR_DI_NONE,
        .encoder_zero_confirm   = 0U,
        .encoder_err_threshold  = 0U,
        .encoder_err_check_ms   = 0U,
        .encoder_jump_threshold = 0U,
        .current_high_threshold = 800U,
        .current_low_threshold  = 50U,
        .current_check_delay_ms = 2000U,
        .current_confirm_ms     = 3000U,
        .alarm_code_current     = ALARM_CODE_BRUSH_CURRENT,
        .alarm_code_fault       = ALARM_CODE_VFD_BRUSH,
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
        .has_encoder            = false,
        .encoder_backend        = MOTOR_ENCODER_NONE,
        .encoder_io             = MOTOR_DI_NONE,
        .encoder_zero_io        = MOTOR_DI_NONE,
        .encoder_zero_confirm   = 0U,
        .encoder_err_threshold  = 0U,
        .encoder_err_check_ms   = 0U,
        .encoder_jump_threshold = 0U,
        .current_high_threshold = 800U,
        .current_low_threshold  = 50U,
        .current_check_delay_ms = 2000U,
        .current_confirm_ms     = 3000U,
        .alarm_code_current     = ALARM_CODE_BRUSH_CURRENT,
        .alarm_code_fault       = ALARM_CODE_VFD_BRUSH,
    },
};

#define M8_MOTOR_TABLE_SIZE ((int)(sizeof(m8_motor_table) / sizeof(m8_motor_table[0])))

#endif /* CONFIG_MACHINE_M8_MOTOR_TABLE_H */
