/**
 * @file    motor.h
 * @brief   通用电机管理层接口
 * @author  HUWANGWEI
 * @date    2026-04-13
 *
 * @note    对上提供 hold / move / stop 等语义化 API，对下通过 hal_motor_ops_t
 *          访问驱动器、编码器与限位。电机差异由机型配置表（motor_cfg_t）描述，
 *          通用状态机与 tick 监测逻辑在本模块内复用。
 */

#ifndef DOMAIN_DEVICE_MOTOR_H
#define DOMAIN_DEVICE_MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 状态与类型
 * ------------------------------------------------------------------------- */

/**
 * @brief  电机状态机状态
 */
typedef enum
{
    MOTOR_STATE_IDLE = 0,  /**< 停止，无驱动器输出 */
    MOTOR_STATE_PENDING,   /**< 等待 post_stop_delay 到期后由 tick 调 pre_start 并启动 */
    MOTOR_STATE_HOLD,      /**< 持续运行（MOTOR_ACTION_HOLD） */
    MOTOR_STATE_MOVE,      /**< 运动中，直至限位/超时/手动停止（MOTOR_ACTION_MOVE） */
    MOTOR_STATE_FAULT,     /**< 故障安全态，已切断输出 */
    MOTOR_STATE_MAX
} motor_state_t;

/**
 * @brief  电机动作完成回调
 * @param  motor_id  完成的电机 ID
 * @param  result    完成结果（SW_OK 或错误码）
 * @param  ctx       注册时传入的用户上下文
 * @note   仅 MOTOR_ACTION_MOVE 类型电机在限位到达或超时时触发。
 */
typedef void (*motor_done_cb_t)(int motor_id, sw_err_t result, void *ctx);

/**
 * @brief  电机启动前回调
 * @param  motor_id   目标电机 ID
 * @param  start_arg  频率模式：速度参考；挡位模式：带符号挡位（正=正转，负=反转）
 * @param  ctx        注册时传入的用户上下文
 * @return SW_OK 表示可继续启动；其它错误码中止本次启动并进入 FAULT
 * @note   在 post_stop_delay 满足后、驱动器输出前由 tick 线程在锁外调用；禁止阻塞。
 */
typedef sw_err_t (*motor_pre_start_cb_t)(int motor_id, int start_arg, void *ctx);

/**
 * @brief  通用速度挡位枚举
 * @note   机型适配层负责映射到 HAL 驱动层的具体挡位值。
 */
typedef enum
{
    MOTOR_GEAR_1 = 1, /**< 最低档 */
    MOTOR_GEAR_2,
    MOTOR_GEAR_3,
    MOTOR_GEAR_4,
} motor_gear_t;

/* -------------------------------------------------------------------------
 * 生命周期
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化电机模块
 * @return SW_OK 成功；SW_ERR_NOT_INIT 表示 HAL 未注册；SW_ERR_PARAM 配置表 ID 非法
 * @note   从机型配置表加载 motor_cfg_t，所有电机初始状态为 IDLE。
 */
sw_err_t motor_init(void);

/**
 * @brief  启动 motor 周期监测线程
 * @return SW_OK 成功；SW_ERR_HW 表示线程创建失败
 * @note   周期 10ms，负责编码器采样、驱动器监测、PENDING 延迟启动与完成回调。
 */
sw_err_t motor_tick_start(void);

/* -------------------------------------------------------------------------
 * 运动控制
 * ------------------------------------------------------------------------- */

/**
 * @brief  以频率模式持续运行电机
 * @param  id         电机 ID
 * @param  speed_ref  速度参考（>0 正转，<0 反转，0 等效于 motor_stop）
 * @return SW_OK 成功；SW_ERR_PARAM 动作类型不匹配；SW_ERR_STATE 非法状态迁移
 * @note   仅适用于 MOTOR_ACTION_HOLD 类型电机。
 */
sw_err_t motor_hold(int id, int speed_ref);

/**
 * @brief  以挡位模式持续运行电机
 * @param  id    电机 ID
 * @param  gear  目标挡位（MOTOR_GEAR_1 = 最低档，始终正转）
 * @return SW_OK 成功；SW_ERR_PARAM 动作类型不匹配或 gear 非法
 * @note   仅适用于 MOTOR_ACTION_HOLD 类型电机。
 */
sw_err_t motor_hold_gear(int id, motor_gear_t gear);

/**
 * @brief  以频率模式运动
 * @param  id         电机 ID
 * @param  speed_ref  速度参考（>0 正转，<0 反转，0 等效于 motor_stop）
 * @return SW_OK 成功；SW_ERR_PARAM 动作类型不匹配；SW_ERR_STATE 非法状态迁移
 * @note   仅适用于 MOTOR_ACTION_MOVE 类型电机；到达限位或超时后触发完成回调。
 */
sw_err_t motor_move(int id, int speed_ref);

/**
 * @brief  以挡位模式运动
 * @param  id    电机 ID
 * @param  gear  目标挡位（MOTOR_GEAR_1 = 最低档）
 * @param  fwd   true=正转，false=反转
 * @return SW_OK 成功；SW_ERR_PARAM 动作类型不匹配或 gear 非法
 * @note   仅适用于 MOTOR_ACTION_MOVE 类型电机。
 */
sw_err_t motor_move_gear(int id, motor_gear_t gear, bool fwd);

/**
 * @brief  停止指定电机
 * @param  id  电机 ID
 * @return SW_OK 成功；SW_ERR_NOT_INIT 模块或 ID 未就绪
 * @note   清零驱动器输出并迁移至 IDLE；FAULT 态下也可调用以完成恢复流程。
 */
sw_err_t motor_stop(int id);

/**
 * @brief  复位驱动层故障（如 VFD RST 脉冲）
 * @param  id  电机 ID
 * @return SW_OK 成功；SW_ERR_NOT_SUPPORT 表示驱动层未实现故障复位
 * @note   本接口仅清除驱动器侧故障，不改变电机状态机状态：调用后状态仍为 FAULT。
 *         FAULT 态下 motor_hold / motor_move / motor_hold_gear / motor_move_gear
 *         会被拒绝（返回 SW_ERR_STATE）且不下发任何输出，以保证故障安全。
 *         完整恢复流程：motor_fault_reset() → motor_stop()（FAULT→IDLE）→ 再启动。
 */
sw_err_t motor_fault_reset(int id);

/* -------------------------------------------------------------------------
 * 编码器与限位
 * ------------------------------------------------------------------------- */

/**
 * @brief  获取编码器累计位置
 * @param  id  电机 ID
 * @return 累计位置（脉冲数）；无编码器或未初始化时返回 -1
 */
int32_t motor_get_pos(int id);

/**
 * @brief  清零编码器（软件位置与硬件脉冲计数器）
 * @param  id  电机 ID
 * @return SW_OK 成功；SW_ERR_PARAM 无编码器；SW_ERR_NOT_SUPPORT 不支持 HW 清零
 */
sw_err_t motor_clear_encoder(int id);

/**
 * @brief  查询是否触发正转限位
 * @param  id  电机 ID
 * @return true 已触发；HAL 未实现或未配置限位时返回 false
 */
bool motor_at_fwd_limit(int id);

/**
 * @brief  查询是否触发反转限位
 * @param  id  电机 ID
 * @return true 已触发；HAL 未实现或未配置限位时返回 false
 */
bool motor_at_rev_limit(int id);

/* -------------------------------------------------------------------------
 * 状态查询
 * ------------------------------------------------------------------------- */

/**
 * @brief  查询电机当前状态
 * @param  id  电机 ID
 * @return 当前状态；ID 非法或未初始化时返回 MOTOR_STATE_FAULT
 */
motor_state_t motor_get_state(int id);

/**
 * @brief  查询电机是否正在运行
 * @param  id  电机 ID
 * @return true 处于 HOLD 或 MOVE；PENDING / IDLE / FAULT 均返回 false
 */
bool motor_is_running(int id);

/**
 * @brief  获取电机负载电流
 * @param  id  电机 ID
 * @return 驱动器负载电流缓存值，单位 0.1A；未运行时返回 0
 * @note   由 motor_tick 周期采样更新。
 */
uint16_t motor_get_load_current(int id);

/* -------------------------------------------------------------------------
 * 回调注册
 * ------------------------------------------------------------------------- */

/**
 * @brief  注册电机动作完成回调
 * @param  motor_id  目标电机 ID
 * @param  cb        回调函数（传 NULL 可清除注册）
 * @param  ctx       透传给回调的用户上下文
 * @return SW_OK 成功；SW_ERR_PARAM 表示 motor_id 非法
 * @note   每个 motor_id 只允许注册一个回调；重复注册覆盖旧值。
 *         回调在 motor_tick 线程上下文中调用，禁止长时间阻塞。
 */
sw_err_t motor_set_done_cb(int motor_id, motor_done_cb_t cb, void *ctx);

/**
 * @brief  注册电机启动前回调
 * @param  motor_id  目标电机 ID
 * @param  cb        回调函数（传 NULL 可清除注册）
 * @param  ctx       透传给回调的用户上下文
 * @return SW_OK 成功；SW_ERR_PARAM 表示 motor_id 非法
 * @note   每个 motor_id 只允许注册一个回调；重复注册覆盖旧值。
 *         回调在 motor_tick 线程上下文中调用（s_mutex 释放后），禁止长时间阻塞。
 *         典型用途：共享 VFD + 接触器切换，在 VFD 输出前完成接触器动作。
 */
sw_err_t motor_set_pre_start_cb(int motor_id, motor_pre_start_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MOTOR_H */
