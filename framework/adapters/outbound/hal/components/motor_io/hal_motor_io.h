/**
 * @file    hal_motor_io.h
 * @brief   通用电机 HAL 内部接口（仅供机型适配层绑定实例）
 * @author  HUWANGWEI
 * @date    2026-06-15
 *
 * @note    仅供 projects/<project>/wiring/ 在 bootstrap 阶段调用；
 *          业务层仍通过 hal_motor_port 访问。
 *          本文件不含任何平台专属 SDK 依赖，也不依赖 hal_vfd_port；
 *          VFD 速度控制及诊断操作通过绑定时注入的回调实现，可用于任何
 *          已注册 hal_io_port 的目标平台。
 */

#ifndef ADAPTERS_HAL_COMPONENTS_MOTOR_IO_HAL_MOTOR_IO_H
#define ADAPTERS_HAL_COMPONENTS_MOTOR_IO_HAL_MOTOR_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/adapters/outbound/hal/components/motor_io/hal_motor_io_bind.h"
#include "framework/common/sw_error.h"

/** @brief 注册通用电机 HAL 实现到 hal_motor_port */
void hal_motor_io_register(void);

/**
 * @brief  绑定指定 motor_id 的硬件配置并初始化驱动实例
 * @param  motor_id  与 domain motor id 一致
 * @param  cfg       绑定配置
 */
sw_err_t hal_motor_io_bind(int motor_id, const hal_motor_io_bind_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_COMPONENTS_MOTOR_IO_HAL_MOTOR_IO_H */
