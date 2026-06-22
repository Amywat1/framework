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

/**
 * @brief  电机动作完成回调
 * @param  motor_id  完成的电机 ID
 * @param  result    完成结果（SW_OK 或错误码）
 * @param  ctx       注册时传入的用户上下文
 */
typedef void (*motor_done_cb_t)(int motor_id, sw_err_t result, void *ctx);

sw_err_t      motor_init(void);
sw_err_t      motor_move(int id, int speed_ref);
sw_err_t      motor_stop(int id);
sw_err_t      motor_fault_reset(int id);
int32_t       motor_get_pos(int id);
sw_err_t      motor_clear_encoder(int id);
bool          motor_at_fwd_limit(int id);
bool          motor_at_rev_limit(int id);
void         *motor_tick_loop(void *arg);

/**
 * @brief  注册电机动作完成回调
 * @note   每个 motor_id 只允许注册一个回调；重复注册覆盖旧值。
 *         回调在 motor_tick 线程上下文中调用，禁止长时间阻塞。
 * @param  motor_id  目标电机 ID
 * @param  cb        回调函数（传 NULL 可清除注册）
 * @param  ctx       透传给回调的用户上下文
 */
sw_err_t      motor_set_done_cb(int motor_id, motor_done_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MOTOR_H */
