/**
 * @file    motor_exec_port.h
 * @brief   电机执行器出站命令与查询。
 *
 * 仅供 domain/mechanism/motor 实现、motor_axis.c 与执行器单测使用。
 * 项目业务模块应 include motor_axis.h，不要直接调用本头中的命令。
 * 接线期绑定硬件端口见 motor_hw_port.h 与 motor_executor_bind / mechanism_bridge_bind。
 */
#ifndef DOMAIN_PORTS_OUTBOUND_MOTOR_MOTOR_EXEC_PORT_H
#define DOMAIN_PORTS_OUTBOUND_MOTOR_MOTOR_EXEC_PORT_H

#include "domain/ports/outbound/motor/motor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 锁存运动目标（异步）。spec 为 NULL 表示连续运行，否则按到位条件结束。
 * @note   再调用即更新目标，调用方不必按状态选择命令。
 * @note   需确认 FAULT、ESTOP、fatal、急停 hold、看门狗安全态必须拒绝。
 *         可续动非 fatal FAULT：参数与互锁校验通过后，在本调用内完成 recover 两步再启动；
 *         校验失败不得内清。
 * @note   终止事件按电机分槽；满时只丢该电机最旧事件。
 */
motor_cmd_result_t motor_exec_run(motor_exec_t            *exec,
                                  int                      motor,
                                  motor_speed_t            spd,
                                  motor_dir_t              dir,
                                  const motor_move_spec_t *spec);

/** @brief 减速停止（异步）。 */
motor_cmd_result_t motor_exec_stop(motor_exec_t *exec, int motor);

/**
 * @brief  回原点便利命令（执行器默认慢速/方向 + ORIGIN 限位）
 * @note   触原点后的基准重建由执行器完成；也可用 run 显式指定方向与速度。
 * @note   可续动非 fatal FAULT 与 `run` 相同：本调用内先完成 recover 两步再启动。
 */
motor_cmd_result_t motor_exec_home(motor_exec_t *exec, int motor);

/** @brief 两步恢复：驱动器复位 → 模块停止（之后由调用方重新启动）。 */
motor_cmd_result_t motor_exec_recover(motor_exec_t *exec, int motor, motor_exec_recovery_step_t step);

/** @brief 手动清零编码器（同步硬件计数器，失败自动重试）。 */
motor_cmd_result_t motor_exec_zero_encoder(motor_exec_t *exec, int motor);

/** @brief 上层显式确认位置基准可信。 */
motor_cmd_result_t motor_exec_confirm_baseline(motor_exec_t *exec, int motor);

/* 查询 —— motor 应在 [0, motor_count) 范围内；越界时返回各函数注释中的安全默认值。 */

/** @brief 查询电机当前状态；电机号越界返回 STOPPED。 */
motor_exec_state_t motor_exec_state(const motor_exec_t *exec, int motor);

/** @brief 查询电机累计位置（脉冲）；电机号越界返回 0。 */
int64_t motor_exec_position(const motor_exec_t *exec, int motor);

/** @brief 查询电机当前运动方向；电机号越界返回 FORWARD。 */
motor_dir_t motor_exec_direction(const motor_exec_t *exec, int motor);

/** @brief 查询电机当前故障码；无故障、句柄无效或电机号越界时为 MOTOR_FAULT_NONE。 */
motor_exec_fault_code_t motor_exec_fault_code(const motor_exec_t *exec, int motor);

/**
 * @brief  查询指定故障码在该电机上是否需确认后才能再运动
 * @param  exec  执行器句柄
 * @param  motor 电机号
 * @param  code  故障码
 * @return true 需显式 recover；false 可续动（下一次 run/home 内清）
 * @note   `MOTOR_FAULT_DRIVER_PORT_FATAL` 恒为需确认。句柄无效或电机号越界时返回 true（偏保守）。
 */
bool motor_exec_fault_requires_confirm(const motor_exec_t *exec, int motor, motor_exec_fault_code_t code);

/**
 * @brief  查询编码器健康状态。
 * @return true 编码器读数可信；false 已检测到停滞或跳变，尚未经归位恢复。
 * @note   电机号越界时返回 false。无编码器的机构恒为 true。这是跨运动的持续状态，供上层作为报警条件；
 *         与"位置基准是否已建立"不同，增量编码器上电时基准未建立但编码器健康。
 */
bool motor_exec_encoder_healthy(const motor_exec_t *exec, int motor);

/**
 * @brief  查询位置基准是否可信。
 * @return true 已通过回原点或执行器 confirm_baseline 建立可信基准；
 *         false 时禁止依赖按位置到位（执行器侧也会拒绝）。
 * @note   电机号越界时返回 false。无编码器的机构恒为 true。与 encoder_healthy 正交：上电后编码器可健康但基准未建。
 */
bool motor_exec_baseline_trusted(const motor_exec_t *exec, int motor);

/**
 * @brief  查询电机当前输出频率
 * @return RUNNING 且速度为频率时返回厘赫，否则 0；句柄无效或越界为 0
 */
int motor_exec_current_freq(const motor_exec_t *exec, int motor);

/**
 * @brief  取出一条运动结束事件，用于记录状态变化原因。
 * @param  exec 电机执行器句柄。
 * @param  out  输出事件；仅在返回 true 时有效。
 * @return true 取出一条事件；false 各电机槽均空。
 * @note   事件按电机分槽；满时只丢该电机最旧事件，不影响其它电机。
 * @note   经 motor_axis 管理的电机应由 axis poll 独占消费（见 pop_event_for）。
 */
bool motor_exec_pop_event(motor_exec_t *exec, motor_event_t *out);

/**
 * @brief  取出指定电机的下一条事件，保留其它电机事件。
 * @param  exec  电机执行器句柄。
 * @param  motor 电机号。
 * @param  out   输出事件；仅在返回 true 时有效。
 * @return true 取出；false 该电机无待取事件。
 */
bool motor_exec_pop_event_for(motor_exec_t *exec, int motor, motor_event_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_MOTOR_MOTOR_EXEC_PORT_H */
