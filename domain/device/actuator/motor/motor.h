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
#include <stdbool.h>

typedef enum
{
    MOTOR_STATE_IDLE = 0,
    MOTOR_STATE_HOLD,
    MOTOR_STATE_MOVE,
    MOTOR_STATE_MOVE_POS,
    MOTOR_STATE_MOVE_TIME,
    MOTOR_STATE_PAUSE,
    MOTOR_STATE_FAULT,
    MOTOR_STATE_MAX
} motor_state_t;

#define MOTOR_DONE_PARAM_PACK(id_, result_)                                           \
    ((uint32_t)((uint16_t)(id_)) |                                                    \
     (((uint32_t)(uint16_t)(int16_t)(result_)) << 16))

#define MOTOR_DONE_PARAM_ID(param_)        ((int)((uint16_t)((param_) & 0xFFFFU)))
#define MOTOR_DONE_PARAM_RESULT(param_)    ((sw_err_t)(int16_t)(((param_) >> 16) & 0xFFFFU))

#define MOTOR_EVENT_PARAM_PACK(id_, info_)                                           \
    ((uint32_t)((uint16_t)(id_)) |                                                    \
     (((uint32_t)(uint16_t)(int16_t)(info_)) << 16))

#define MOTOR_EVENT_PARAM_ID(param_)       ((int)((uint16_t)((param_) & 0xFFFFU)))
#define MOTOR_EVENT_PARAM_INFO(param_)     ((int)((int16_t)(((param_) >> 16) & 0xFFFFU)))

sw_err_t      motor_init(void);
sw_err_t      motor_move(int id, int speed_ref);
sw_err_t      motor_stop(int id);
sw_err_t      motor_fault_reset(int id);
int32_t       motor_get_pos(int id);
sw_err_t      motor_clear_encoder(int id);
bool          motor_at_fwd_limit(int id);
bool          motor_at_rev_limit(int id);
void         *motor_tick_loop(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MOTOR_H */
