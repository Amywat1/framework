/**
 * @file    mechanism_bridge.h
 * @brief   机构控制应用桥接：登记电机 tick 与水路 poll 两拍周期任务
 *
 * 独立目标 wdf_mechanism_bridge，不并入 wdf_application。
 * 项目在 composition root 绑定执行器、加入轴（保存返回的轴句柄）后调用 register_tasks。
 * 命令与 poll 必须使用同一 motor_axis_t，否则事件会被桥接抽空。
 */
#ifndef APPLICATION_BRIDGES_MECHANISM_BRIDGE_H
#define APPLICATION_BRIDGES_MECHANISM_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/mechanism/patterns/motor_axis.h"
#include "domain/ports/outbound/motor/motor_exec_port.h"

/**
 * @brief  绑定电机执行器句柄，供周期任务推进 tick
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
 * @brief  登记电机 tick 与水路 poll 两拍周期任务
 * @note   可重复调用：已登记的任务跳过。电机未绑定时空转 tick；水路未 init 时 poll 为空操作。
 * @note   两拍分别登记；其中一拍失败时已成功的那拍保留，下次只补登记失败项。
 * @return SW_OK 成功；周期任务登记失败时返回其错误码
 */
sw_err_t mechanism_bridge_register_tasks(void);

/**
 * @brief  对已登记轴下发停止
 */
void mechanism_bridge_halt_all(void);

#ifdef MECHANISM_BRIDGE_UNIT_TEST
/**
 * @brief  测试复位：清空绑定、轴表与任务登记旗标
 */
void mechanism_bridge_reset_for_test(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_BRIDGES_MECHANISM_BRIDGE_H */
