/**
 * @file    m8_brush_setup.h
 * @brief   M8 机型刷子控制初始化接口。
 *
 * 负责创建并配置 MCC 电机执行器，绑定 VFD 驱动端口与接触器回调，
 * 完成后调用 brush_init() 将执行器注入领域层。
 *
 * @note 调用前置条件：
 *   1. m8_vfd_setup() 已完成（HAL_VFD_BRUSH 已初始化）；
 *   2. hal_io 已初始化（DO 写接口已注册）。
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_BRUSH_SETUP_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_BRUSH_SETUP_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 M8 刷子控制模块。
 *
 * 完成以下步骤：
 *   1. 配置 MCC 执行器（1 个电机、1 个 VFD 驱动器、挡位与监测参数）；
 *   2. 注册 VFD 驱动端口回调（调用 hal_vfd）；
 *   3. 注册接触器操作回调（驱动 SIDE_BRUSH_ACT / TOP_BRUSH_ACT DO）；
 *   4. 调用 brush_init() 注入执行器；
 *   5. 调用 m8_motor_tick_register(brush_tick) 注册到统一 tick 管理器。
 *
 * @return SW_OK 成功；SW_ERR_NOT_INIT 前置依赖未就绪；SW_ERR_HW 初始化失败。
 */
sw_err_t m8_brush_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_BRUSH_SETUP_H */
