/**
 * @file    mechanism_bridge.h
 * @brief   机构控制应用桥接：绑定执行器、出厂轴实例、登记周期任务
 *
 * 独立目标 wdf_mechanism_bridge，不并入 wdf_application。
 * 接线：bind（或 bind_motor）→ add_axis（保存返回的轴句柄）
 *       → add_post_tick（可选，多轴协调器）→ register_tasks。
 * 业务命令与查询只用 motor_axis_t，不要再持有 motor_exec_t。
 * 电机拍顺序（稳定契约）：
 *   motor_executor_tick → 已登记轴 motor_axis_poll → 可选 post-tick。
 * post-tick 为通用多轴协调扩展点，非共享驱动专用；无协调需求可不登记。
 * MUTEX 互锁语义为拒令，不自动让路；prepare 仅表示本电机启动前硬件预备。
 */
#ifndef APPLICATION_BRIDGES_MECHANISM_BRIDGE_H
#define APPLICATION_BRIDGES_MECHANISM_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/mechanism/motor/motor_executor.h"
#include "domain/mechanism/patterns/motor_axis.h"

/**
 * @brief  绑定配置与硬件端口到执行器槽，并交给本桥接
 * @param  slot_id 执行器槽位
 * @param  cfg     电机配置
 * @param  ports   硬件端口表
 * @return 与 motor_executor_bind 相同；成功后即可 add_axis
 */
motor_init_result_t mechanism_bridge_bind(unsigned slot_id, const motor_config_t *cfg, const motor_ports_t *ports);

/**
 * @brief  绑定已完成 motor_executor_bind 的句柄（单测假执行器或未走 bind 的接线）
 * @param  exec 已 bind 的执行器；不可为空
 * @return SW_OK 成功；SW_ERR_PARAM / SW_ERR_STATE 失败
 */
sw_err_t mechanism_bridge_bind_motor(motor_exec_t *exec);

/**
 * @brief  登记一根由本桥接 poll 的轴，并返回该实例供命令使用
 * @param  motor    电机号
 * @param  opts     lifecycle 选项，可为 NULL
 * @param  out_axis 成功时写入轴指针；可为 NULL
 * @return SW_OK 成功；SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_STATE / SW_ERR_OVERFLOW 失败
 * @note   同一电机不可重复登记。项目须用返回的轴做 run/home/stop，不要另建 motor_axis_t。
 */
sw_err_t mechanism_bridge_add_axis(int motor, const motion_lifecycle_opts_t *opts, motor_axis_t **out_axis);

/**
 * @brief  按电机号取桥接持有的轴
 * @return 已登记的轴；未找到为 NULL
 */
motor_axis_t *mechanism_bridge_axis(int motor);

/**
 * @brief  电机拍后钩子（在全部已登记轴 poll 之后调用）
 * @param  ctx  登记时传入的上下文
 */
typedef void (*mechanism_post_tick_fn_t)(void *ctx);

/**
 * @brief  登记多轴协调器，挂在电机拍的轴 poll 之后
 * @param  fn  回调；不可为空
 * @param  ctx 透传上下文，可为 NULL
 * @return SW_OK 成功；SW_ERR_PARAM / SW_ERR_STATE（同一 fn 已登记）/ SW_ERR_OVERFLOW 失败
 * @note   用于仿形、到位联锁等多轴策略（须先看到本拍结局再决策），不要另开周期任务。
 *         共享物理驱动的路径切换应在项目内按单电机槽建模，不必强依赖本钩子。
 */
sw_err_t mechanism_bridge_add_post_tick(mechanism_post_tick_fn_t fn, void *ctx);

/**
 * @brief  登记电机 tick 与水路 poll 两拍周期任务
 * @note   可重复调用：已登记的任务跳过。电机未绑定时空转 tick；水路未 init 时 poll 为空操作。
 * @note   两拍分别登记；其中一拍失败时已成功的那拍保留，下次只补登记失败项。
 * @note   电机拍内顺序：`motor_executor_tick` → 各轴 `motor_axis_poll` → post-tick。
 * @return SW_OK 成功；周期任务登记失败时返回其错误码
 */
sw_err_t mechanism_bridge_register_tasks(void);

/**
 * @brief  对已登记轴下发停止
 */
void mechanism_bridge_halt_all(void);

/**
 * @brief  致命故障后按首次 bind 的 cfg/ports 重新初始化执行器
 */
motor_init_result_t mechanism_bridge_reinit(void);

/**
 * @brief  看门狗安全态解除（须 tick 节拍已恢复）
 */
void mechanism_bridge_reset_watchdog(void);

/**
 * @brief  查询执行器是否处于看门狗安全态
 */
bool mechanism_bridge_in_safe_state(void);

#ifdef MECHANISM_BRIDGE_UNIT_TEST
/**
 * @brief  测试复位：清空绑定、轴表、post-tick 钩子与任务登记旗标
 */
void mechanism_bridge_reset_for_test(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_BRIDGES_MECHANISM_BRIDGE_H */
