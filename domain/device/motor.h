/**
 * @file    motor.h
 * @brief   通用电机管理层接口
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#ifndef DOMAIN_DEVICE_MOTOR_H
#define DOMAIN_DEVICE_MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdint.h>

typedef enum
{
    MOTOR_STATE_IDLE = 0,
    MOTOR_STATE_HOLD,
    MOTOR_STATE_MOVE,
    MOTOR_STATE_MOVE_POS,
    MOTOR_STATE_MOVE_TIME,
    MOTOR_STATE_PAUSE,
    MOTOR_STATE_STOP,
    MOTOR_STATE_FAULT,
    MOTOR_STATE_MAX
} motor_state_t;

#define MOTOR_DONE_PARAM_PACK(id_, result_)                                           \
    ((uint32_t)((uint16_t)(id_)) |                                                    \
     (((uint32_t)(uint16_t)(int16_t)(result_)) << 16))

#define MOTOR_DONE_PARAM_ID(param_)        ((int)((uint16_t)((param_) & 0xFFFFU)))
#define MOTOR_DONE_PARAM_RESULT(param_)    ((sw_err_t)(int16_t)(((param_) >> 16) & 0xFFFFU))

sw_err_t      motor_init(void);
sw_err_t      motor_hold(int id, int speed_ref);
sw_err_t      motor_move(int id, int speed_ref);
sw_err_t      motor_move_pos(int id, int speed_ref, int32_t target_pos);
sw_err_t      motor_move_time(int id, int speed_ref, uint32_t duration_ms);
sw_err_t      motor_pause(int id);
sw_err_t      motor_resume(int id);
sw_err_t      motor_stop(int id);
sw_err_t      motor_reset_fault(int id);
motor_state_t motor_get_state(int id);
void         *motor_tick_loop(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MOTOR_H */
