/**
 * @file    hal_motor_io_bind.h
 * @brief   电机 HAL 绑定配置（机型适配层 → HAL 实现）
 * @author  HUWANGWEI
 * @date    2026-06-15
 */

#ifndef ADAPTERS_HAL_COMPONENTS_MOTOR_IO_HAL_MOTOR_IO_BIND_H
#define ADAPTERS_HAL_COMPONENTS_MOTOR_IO_HAL_MOTOR_IO_BIND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"
#include "framework/common/vfd_types.h"
#include <stdbool.h>
#include <stdint.h>

#define HAL_MOTOR_IO_SLOT_MAX  8

/**
 * @brief  频率速度回调（VFD 频率模式启停）
 * @param  speed_ref  >0 正转，<0 反转，0 停止；绝对值为频率量纲（由注入方定义）
 * @param  ctx        注入方传入的上下文（如 vfd_id）
 */
typedef sw_err_t (*hal_motor_set_speed_fn)(int speed_ref, void *ctx);

/**
 * @brief  挡位控制回调（VFD 挡位模式启动）
 * @param  gear  正值=正转，负值=反转，0=停止；绝对值为挡位号（1=最低档）
 * @param  ctx   注入方传入的上下文
 */
typedef sw_err_t (*hal_motor_set_gear_fn)(hal_vfd_gear_t gear, void *ctx);

/**
 * @brief  读取单个数值回调（电流 / 状态字）
 * @param  p_val  输出缓冲
 * @param  ctx    注入方传入的上下文
 */
typedef sw_err_t (*hal_motor_read_val_fn)(uint16_t *p_val, void *ctx);

/**
 * @brief  无参数动作回调（故障复位）
 * @param  ctx  注入方传入的上下文
 */
typedef sw_err_t (*hal_motor_action_fn)(void *ctx);

typedef struct
{
    /**
     * @brief  频率速度输出回调；NULL 时退化为纯 DO 方向控制
     * @note   有 VFD 频率模式的项目由机型 setup 注入；无 VFD 的项目保持 NULL
     */
    hal_motor_set_speed_fn set_speed;
    /**
     * @brief  挡位控制回调；NULL → motor_hold_gear 返回 SW_ERR_NOT_SUPPORT
     */
    hal_motor_set_gear_fn  set_gear;
    hal_motor_read_val_fn  read_current; /**< 读负载电流；NULL → SW_ERR_NOT_SUPPORT */
    hal_motor_read_val_fn  read_status;  /**< 读运行状态字；NULL → SW_ERR_NOT_SUPPORT */
    hal_motor_action_fn    fault_reset;  /**< 故障复位；NULL → SW_ERR_NOT_SUPPORT */
    void                  *drv_ctx;      /**< 透传给上述四个回调的上下文 */

    io_do_t  io_cw;
    io_do_t  io_ccw;
    io_do_t  io_stop;
    io_do_t  io_vel0;
    io_do_t  io_vel1;

    io_di_t  limit_io_cw;
    io_di_t  limit_io_ccw;

    bool     has_encoder;
    io_di_t  encoder_io;
} hal_motor_io_bind_cfg_t;

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_COMPONENTS_MOTOR_IO_HAL_MOTOR_IO_BIND_H */
