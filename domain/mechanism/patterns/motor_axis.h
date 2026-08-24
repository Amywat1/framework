/**
 * @file    motor_axis.h
 * @brief   单轴运动会话（连续运转 / 到位 / 故障恢复）
 *
 * @note    项目业务只面对本头：命令、查询、结局均走轴实例。执行器命令入口
 *          不在本头出现。生产路径由 mechanism_bridge_add_axis 出厂；
 *          motor_axis_init 供桥接与单测。
 * @note    方向由调用方显式传入；机构“只正转”等约束属于项目 mechanism 层。
 * @note    run 为异步命令：受理后即返回。完成须 poll 至非 MOVING，或依赖
 *          桥接内的 motor_axis_poll()；后者先消费本电机事件并回调 on_motion_end，
 *          再在进入 IDLE 时发布 motion_completed（仅表示轴已空闲）。
 * @note    经本模式管理的电机由轴 poll 独占消费执行器事件。
 * @note    on_motion_end 对同一故障闩锁（同 type + fault）去重；run/home/recover
 *          受理后解除闩锁。查询只转发执行器，轴上不缓存位置或故障码。
 * @note    回原用 motor_axis_home()，方向取执行器配置 home_dir。
 */

#ifndef DOMAIN_MECHANISM_PATTERNS_MOTOR_AXIS_H
#define DOMAIN_MECHANISM_PATTERNS_MOTOR_AXIS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/mechanism/model/actuator_events.h"
#include "domain/ports/outbound/motor/motor_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief  运动结局回调（项目层做流程分支或报警映射）
 * @param  id  注入的 actuator_id；可为 0
 * @param  ev  执行器终止事件，非 NULL
 */
typedef void (*motion_process_end_fn_t)(actuator_id_t id, const motor_event_t *ev);

/**
 * @brief  运动模式 lifecycle 注入选项
 * @note   motion_completed 只表示轴空闲（重评估）；结局原因走 on_motion_end / last_result。
 */
typedef struct {
    actuator_id_t           motion_actuator_id;
    motion_process_end_fn_t on_motion_end;
} motion_lifecycle_opts_t;

typedef enum {
    MOTOR_AXIS_STATE_IDLE = 0,
    MOTOR_AXIS_STATE_MOVING,
    MOTOR_AXIS_STATE_FAULT,
} motor_axis_state_t;

typedef struct {
    motor_exec_t           *exec;
    int                     motor;
    motion_lifecycle_opts_t opts;
    bool                    inited;
    bool                    awaiting_idle; /**< 有未完成的运动/停止/恢复意图，待进入 IDLE */
    bool                    last_valid;    /**< last_event 是否曾记录过结局 */
    motor_event_t           last_event;    /**< 最近一次终止结局（含命令拒令合成） */
} motor_axis_t;

/**
 * @brief  初始化单轴运动会话
 * @param[in,out] self   模式实例
 * @param[in]     exec   电机执行器句柄
 * @param[in]     motor  电机索引
 * @param[in]     opts   lifecycle 选项，可为 NULL
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法
 * @note   生产路径请用 mechanism_bridge_add_axis，不要在业务侧另建一份再 init。
 */
sw_err_t motor_axis_init(motor_axis_t *self, motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts);

/**
 * @brief  启动运动：spec 为 NULL 时连续运转，否则按 spec 到位
 * @param[in,out] self   模式实例
 * @param[in]     dir    运动方向
 * @param[in]     speed  速度设定，必须大于 0
 * @param[in]     spec   到位条件，NULL 表示连续运转
 * @return 命令结果；未初始化为 UNAVAILABLE
 * @note   再次调用会更新目标，由执行器收敛
 * @note   需确认 FAULT 时拒绝，须先 motor_axis_recover()；
 *         可续动非 fatal FAULT 由执行器在本次命令内清除后再启动
 */
motor_cmd_result_t motor_axis_run(motor_axis_t            *self,
                                  motor_dir_t              dir,
                                  motor_speed_t            speed,
                                  const motor_move_spec_t *spec);

/**
 * @brief  受控停止
 * @param[in,out] self 模式实例
 * @return 命令结果；未初始化为 UNAVAILABLE
 */
motor_cmd_result_t motor_axis_stop(motor_axis_t *self);

/**
 * @brief  查询运动状态（由执行器运行状态映射）
 * @param[in] self 模式实例
 * @return 当前状态；未初始化时返回 IDLE
 */
motor_axis_state_t motor_axis_state(const motor_axis_t *self);

/**
 * @brief  回原点（方向取执行器配置 home_dir，须为正向或反向）
 * @param[in,out] self 模式实例
 * @return 命令结果；未初始化为 UNAVAILABLE
 */
motor_cmd_result_t motor_axis_home(motor_axis_t *self);

/**
 * @brief  查询最近一次终止结局
 * @param[in]  self 模式实例
 * @param[out] out  结局事件；仅返回 true 时有效
 * @return true 曾记录过结局；未初始化、尚无结局或 out 为空时为 false
 */
bool motor_axis_last_result(const motor_axis_t *self, motor_event_t *out);

/**
 * @brief  执行故障恢复（驱动器复位 + 模块停止）
 * @param[in,out] self 模式实例
 * @return 命令结果；任一步失败即返回该步结果
 */
motor_cmd_result_t motor_axis_recover(motor_axis_t *self);

/**
 * @brief  推进事件与空闲边沿
 * @param[in,out] self 模式实例
 * @note   生产路径由 mechanism_bridge 的电机拍调用；单测可手动推进
 */
void motor_axis_poll(motor_axis_t *self);

/**
 * @brief  轴是否已结算
 * @param[in] self 模式实例
 * @return true 已初始化、非 MOVING、且无待消费结局；未初始化为 false
 */
bool motor_axis_is_settled(const motor_axis_t *self);

/**
 * @brief  查询逻辑位置（脉冲）
 * @note   直接转发执行器，轴上不缓存
 */
int64_t motor_axis_position(const motor_axis_t *self);

/**
 * @brief  查询当前运动方向
 * @return 未初始化时为 FORWARD
 */
motor_dir_t motor_axis_direction(const motor_axis_t *self);

/**
 * @brief  查询当前故障码
 * @return 未初始化或无故障为 MOTOR_FAULT_NONE
 */
motor_exec_fault_code_t motor_axis_fault_code(const motor_axis_t *self);

/**
 * @brief  查询指定故障码在本轴是否需确认
 * @return 未初始化时 true（偏保守）
 */
bool motor_axis_fault_requires_confirm(const motor_axis_t *self, motor_exec_fault_code_t code);

/**
 * @brief  查询位置基准是否可信
 * @return 未初始化为 false
 */
bool motor_axis_baseline_trusted(const motor_axis_t *self);

/**
 * @brief  查询编码器是否健康
 * @return 未初始化为 false
 */
bool motor_axis_encoder_healthy(const motor_axis_t *self);

/**
 * @brief  查询当前输出频率（厘赫）
 * @return 未运行或未初始化为 0
 */
int motor_axis_current_freq(const motor_axis_t *self);

/**
 * @brief  手动清零编码器
 * @return 命令结果；未初始化为 UNAVAILABLE
 */
motor_cmd_result_t motor_axis_zero_encoder(motor_axis_t *self);

/**
 * @brief  显式确认位置基准可信
 * @return 命令结果；未初始化为 UNAVAILABLE
 */
motor_cmd_result_t motor_axis_confirm_baseline(motor_axis_t *self);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_MECHANISM_PATTERNS_MOTOR_AXIS_H */
