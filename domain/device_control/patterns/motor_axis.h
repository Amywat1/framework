/**
 * @file    motor_axis.h
 * @brief   单轴运动模式（连续运转 / 到位 / 故障恢复）
 * @author  HUWANGWEI
 * @date    2026-07-17
 *
 * @note    方向由调用方显式传入；机构“只正转”等约束属于 mechanism 层，
 *          不得在本模式内写死。
 * @note    run 为异步命令：受理后即返回。到位/停止完成后须
 *          poll state == IDLE，或周期调用 motor_axis_poll()；
 *          后者先消费本电机执行器事件并回调 on_motion_end，再在进入 IDLE 时
 *          发布 motion_completed（仅表示轴已空闲）。
 * @note    经本模式管理的电机应由 motor_axis_poll 独占消费其执行器事件，
 *          项目层不要再对该电机调用 hal_motor_pop_event / 注册会排空队列的回调。
 * @note    on_motion_end 对同一故障闩锁（同 outcome + fault）去重；项目层仍宜按码幂等处理。
 * @note    回原不单独提供 API：对 ORIGIN 限位下发 run(..., spec)，
 *          触原点后的基准重建由电机执行器完成。
 * @note    位置/方向/故障码等实时查询仍直接使用 hal_motor_*；结局详情用 last_result。
 */

#ifndef DOMAIN_DEVICE_CONTROL_PATTERNS_MOTOR_AXIS_H
#define DOMAIN_DEVICE_CONTROL_PATTERNS_MOTOR_AXIS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/device_control/patterns/motion_lifecycle.h"
#include "domain/ports/outbound/motor/hal_motor_exec_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MOTOR_AXIS_STATE_IDLE = 0,
    MOTOR_AXIS_STATE_MOVING,
    MOTOR_AXIS_STATE_FAULT,
} motor_axis_state_t;

typedef struct {
    hal_motor_exec_t       *exec;
    int                     motor;
    motion_lifecycle_opts_t opts;
    bool                    inited;
    bool                    awaiting_idle; /**< 有未完成的运动/停止/恢复意图，待进入 IDLE */
    motor_axis_end_result_t last_result;   /**< 最近一次终止结局（含命令拒令合成） */
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
 * @param[in]     speed       速度设定，必须大于 0
 * @param[in]     spec        到位条件，NULL 表示连续运转
 * @return SW_OK 命令已受理；SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_STATE 失败
 * @note   完成须 poll state == IDLE，或依赖 motor_axis_poll() 发空闲事件
 * @note   连续运行期间重复调用会更新速度或方向，不会重新启动运动生命周期
 * @note   FAULT 时由执行器拒绝；调用方须先 motor_axis_recover()
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
 * @brief  查询运动状态（由 HAL phase 映射）
 * @param[in] self 模式实例
 * @return 当前状态；未初始化时返回 IDLE
 */
motor_axis_state_t motor_axis_state(const motor_axis_t *self);

/**
 * @brief  查询最近一次终止结局
 * @param[in] self 模式实例
 * @return 结局详情；未初始化或尚无结局时 valid 为 false
 */
motor_axis_end_result_t motor_axis_last_result(const motor_axis_t *self);

/**
 * @brief  执行故障恢复（驱动器复位 + 模块停止）
 * @param[in,out] self 模式实例
 * @return SW_OK 成功；SW_ERR_NOT_INIT / SW_ERR_STATE 失败
 * @note   内部按序执行 DRIVER_RESET 与 MODULE_STOP；任一步失败即返回
 */
sw_err_t motor_axis_recover(motor_axis_t *self);

/**
 * @brief  推进事件与空闲边沿
 * @param[in,out] self 模式实例
 * @note   先按电机号排空执行器事件：终止类事件写入 last_result 并回调 on_motion_end；
 *         若存在未完成意图且已进入 IDLE，再发布 motion_completed（空闲，不含原因）
 */
void motor_axis_poll(motor_axis_t *self);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_PATTERNS_MOTOR_AXIS_H */
