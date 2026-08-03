/**
 * @file    motor_axis.h
 * @brief   单轴运动模式（连续运转 / 到位 / 回零）
 * @author  HUWANGWEI
 * @date    2026-07-17
 *
 * @note    方向由调用方显式传入；机构“只正转”等约束属于 mechanism 层，
 *          不得在本模式内写死。
 * @note    move_to / home 为异步命令：本模式在命令受理后即返回，不会在
 *          到位瞬间主动发布完成事件。调用方须 poll
 *          motor_axis_state() == MOTOR_AXIS_STATE_IDLE 判定到位完成。
 *          stop / recover 受理成功时可发布 motion_completed（表示结束请求
 *          已被接受，不等于先前 move_to 的到位完成）。
 */

#ifndef DOMAIN_DEVICE_CONTROL_PATTERNS_MOTOR_AXIS_H
#define DOMAIN_DEVICE_CONTROL_PATTERNS_MOTOR_AXIS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/device_control/patterns/motion_lifecycle.h"
#include "ports/outbound/hal/motor/hal_motor_exec_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MOTOR_AXIS_STATE_IDLE = 0,
    MOTOR_AXIS_STATE_MOVING,
    MOTOR_AXIS_STATE_STOPPING,
    MOTOR_AXIS_STATE_FAULT,
} motor_axis_state_t;

typedef struct {
    hal_motor_exec_t       *exec;
    int                     motor;
    motion_lifecycle_opts_t opts;
    bool                    inited;
} motor_axis_t;

/**
 * @brief  初始化单轴运动模式
 * @param[in,out] self   模式实例
 * @param[in]     exec   电机执行器句柄
 * @param[in]     motor  电机索引
 * @param[in]     opts   lifecycle 选项，可为 NULL
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法
 */
sw_err_t motor_axis_init(motor_axis_t *self, hal_motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts);

/**
 * @brief  启动运动：spec 为 NULL 时连续运转，否则按 spec 到位
 * @param[in,out] self        模式实例
 * @param[in]     dir         运动方向
 * @param[in]     speed       速度设定；值为 0 时等价于 stop，运行值必须大于 0
 * @param[in]     spec        到位条件，NULL 表示连续运转
 * @return SW_OK 命令已受理；SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_STATE 失败
 * @note   到位完成须由调用方 poll state == IDLE，见文件头注释
 * @note   speed.value == 0 时直接 stop，不发 run 命令
 * @note   连续运行期间重复调用会更新速度或方向，不会重新启动运动生命周期
 */
sw_err_t motor_axis_run(motor_axis_t                *self,
                        hal_motor_dir_t              dir,
                        hal_motor_speed_t            speed,
                        const hal_motor_move_spec_t *spec);

/**
 * @brief  减速停止
 * @param[in,out] self 模式实例
 * @return SW_OK 成功；SW_ERR_NOT_INIT / SW_ERR_STATE 失败
 */
sw_err_t motor_axis_stop(motor_axis_t *self);

/**
 * @brief  回原点
 * @param[in,out] self 模式实例
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT / SW_ERR_STATE 失败
 * @note   完成判定同 move_to：调用方 poll state == IDLE
 */
sw_err_t motor_axis_home(motor_axis_t *self);

/**
 * @brief  查询运动状态（由 HAL phase 映射）
 * @param[in] self 模式实例
 * @return 当前状态；未初始化时返回 IDLE
 */
motor_axis_state_t motor_axis_state(const motor_axis_t *self);

/**
 * @brief  查询当前方向
 * @param[in] self 模式实例
 * @return 当前方向；未初始化时返回 FORWARD
 */
hal_motor_dir_t motor_axis_direction(const motor_axis_t *self);

/**
 * @brief  查询累计位置（脉冲）
 * @param[in] self 模式实例
 * @return 位置；未初始化时返回 0
 */
int64_t motor_axis_position(const motor_axis_t *self);

/**
 * @brief  查询故障码
 * @param[in] self 模式实例
 * @return 故障码；无故障或未初始化时为 HAL_MOTOR_FAULT_NONE
 */
hal_motor_fault_code_t motor_axis_fault_code(const motor_axis_t *self);

/**
 * @brief  执行恢复步骤
 * @param[in,out] self 模式实例
 * @param[in]     step 恢复步骤
 * @return SW_OK 成功；SW_ERR_NOT_INIT / SW_ERR_STATE 失败
 */
sw_err_t motor_axis_recover(motor_axis_t *self, hal_motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_PATTERNS_MOTOR_AXIS_H */
