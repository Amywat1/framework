/**
 * @file    m8_machine_setup.h
 * @brief   M8 机型装配总入口。
 *
 * 编排 motor_exec、电机领域绑定、水路等 M8 专属初始化，
 * bootstrap 仅调用本接口，不感知具体机构模块。
 *
 * @note 须在 wiring 完成且 hal_vfd / hal_io 已注册后调用。
 *       m8_motor_exec_start() 仍由 bootstrap 在全部 setup 完成后调用。
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_MACHINE_SETUP_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_MACHINE_SETUP_H

#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief M8 机型标准装配：motor_exec + 全部 motor domain + water。
 *
 * @return SW_OK 成功；其余为各子步骤返回值。
 */
sw_err_t m8_machine_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_MACHINE_SETUP_H */
