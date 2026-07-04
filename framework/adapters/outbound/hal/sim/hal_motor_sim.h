/**
 * @file    hal_motor_sim.h
 * @brief   仿真电机 HAL 内部接口（仅供机型适配层绑定实例）
 * @author  HUWANGWEI
 * @date    2026-06-15
 */

#ifndef ADAPTERS_HAL_SIM_HW_HAL_MOTOR_SIM_H
#define ADAPTERS_HAL_SIM_HW_HAL_MOTOR_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/adapters/outbound/hal/components/motor_io/hal_motor_io_bind.h"
#include "framework/common/sw_error.h"

/** @brief 注册 hal_motor_sim 实现�?hal_motor_port */
void hal_motor_sim_register(void);

/**
 * @brief  绑定指定 motor_id 的仿真配置（限位 DI、编码器等）
 * @param  motor_id  �?domain motor id 一�?
 * @param  cfg       绑定配置
 */
sw_err_t hal_motor_sim_bind(int motor_id, const hal_motor_io_bind_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_SIM_HW_HAL_MOTOR_SIM_H */
