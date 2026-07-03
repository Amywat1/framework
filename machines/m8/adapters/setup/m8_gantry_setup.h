/**
 * @file    m8_gantry_setup.h
 * @brief   M8 机型龙门行走控制初始化接口。
 *
 * 负责创建并配置 MCC 电机执行器，绑定 VFD 驱动端口、编码器、
 * 限位传感器回调，完成后调用 gantry_init() 将执行器注入领域层，
 * 并向统一 tick 管理器注册 gantry_tick。
 *
 * @note 调用前置条件：
 *   1. m8_vfd_setup() 已完成（HAL_VFD_GANTRY 已初始化）；
 *   2. hal_io 已初始化（DI 读接口与脉冲计数器已注册）。
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_GANTRY_SETUP_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_GANTRY_SETUP_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 M8 龙门行走控制模块。
 *
 * 完成以下步骤：
 *   1. 配置 MCC 执行器（1 个电机、1 个 VFD 驱动器、编码器、限位、挡位参数）；
 *   2. 注册 VFD 驱动端口回调（调用 hal_vfd + GANTRY_HIGH_SPEED DO）；
 *   3. 注册编码器端口回调（调用 hal_io pulse_read / pulse_clear）；
 *   4. 注册限位端口回调（GANTRY_FWD_LIMIT / GANTRY_REV_LIMIT DI）；
 *   5. 调用 gantry_init() 注入执行器；
 *   6. tick 由 m8_motor_exec_start() 统一驱动，无需单独注册。
 *
 * @return SW_OK 成功；SW_ERR_NOT_INIT 前置依赖未就绪；SW_ERR_HW 初始化失败。
 */
sw_err_t m8_gantry_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_GANTRY_SETUP_H */
