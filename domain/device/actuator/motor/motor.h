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
    MOTOR_STATE_PENDING, /**< 等待 post_stop_delay 到期后由 tick 调 pre_start 并启动 */
    MOTOR_STATE_HOLD,
    MOTOR_STATE_MOVE,
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

/**
 * @brief  电机启动前回调（在 post_stop_delay 满足后、VFD 输出前由 tick 调用）
 * @param  motor_id   目标电机 ID
 * @param  speed_ref  即将设置的速度参考值
 * @param  ctx        注册时传入的用户上下文
 * @return SW_OK 表示可继续启动；其它错误码中止本次启动并进入 FAULT
 */
typedef sw_err_t (*motor_pre_start_fn)(int motor_id, int speed_ref, void *ctx);

/**
 * 通用速度挡位枚举（machine 适配层负责映射到 HAL VFD 的具体挡位寄存器值）
 */
typedef enum
{
    MOTOR_GEAR_1 = 1,
    MOTOR_GEAR_2,
    MOTOR_GEAR_3,
    MOTOR_GEAR_4,
} motor_gear_t;

sw_err_t      motor_init(void);
sw_err_t      motor_tick_start(void);
sw_err_t      motor_hold(int id, int speed_ref);

/**
 * @brief  以挡位模式持续运行电机（MOTOR_ACTION_HOLD 类型专用，始终正转）
 * @param  id    电机 ID
 * @param  gear  目标挡位（MOTOR_GEAR_1 = 最低档）
 */
sw_err_t      motor_hold_gear(int id, motor_gear_t gear);

/**
 * @brief  以挡位模式运动（MOTOR_ACTION_MOVE 类型专用）
 * @param  id    电机 ID
 * @param  gear  目标挡位（MOTOR_GEAR_1 = 最低档）
 * @param  fwd   true=正转，false=反转
 */
sw_err_t      motor_move_gear(int id, motor_gear_t gear, bool fwd);
sw_err_t      motor_move(int id, int speed_ref);
sw_err_t      motor_stop(int id);
sw_err_t      motor_fault_reset(int id);
int32_t       motor_get_pos(int id);
sw_err_t      motor_clear_encoder(int id);
bool          motor_at_fwd_limit(int id);
bool          motor_at_rev_limit(int id);

/**
 * @brief  查询电机当前状态
 */
motor_state_t motor_get_state(int id);

/**
 * @brief  查询电机是否正在运行（HOLD / MOVE）
 * @note   PENDING（延迟启动中）不计入运行，FAULT / IDLE 均返回 false。
 */
bool          motor_is_running(int id);

/**
 * @brief  获取电机负载电流（VFD 电流寄存器缓存值，单位 0.1A）
 * @note   由 motor_tick 周期采样，电机未运行时返回 0。
 */
uint16_t      motor_get_current(int id);

/**
 * @brief  注册电机动作完成回调
 * @note   每个 motor_id 只允许注册一个回调；重复注册覆盖旧值。
 *         回调在 motor_tick 线程上下文中调用，禁止长时间阻塞。
 * @param  motor_id  目标电机 ID
 * @param  cb        回调函数（传 NULL 可清除注册）
 * @param  ctx       透传给回调的用户上下文
 */
sw_err_t      motor_set_done_cb(int motor_id, motor_done_cb_t cb, void *ctx);

/**
 * @brief  注册电机启动前回调
 * @note   每个 motor_id 只允许注册一个回调；重复注册覆盖旧值。
 *         回调在 motor_tick 线程上下文中调用（持有 s_mutex 释放后），禁止长时间阻塞。
 *         典型用途：共享 VFD + 接触器切换，在 VFD 输出前完成接触器动作。
 * @param  motor_id  目标电机 ID
 * @param  cb        回调函数（传 NULL 可清除注册）
 * @param  ctx       透传给回调的用户上下文
 */
sw_err_t      motor_set_pre_start_cb(int motor_id, motor_pre_start_fn cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MOTOR_H */
