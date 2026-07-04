/**
 * @file    hal_vfd.h
 * @brief   VFD HAL 通用组合层（bind + 通信监测 + 复位脉冲编排）
 * @author  HUWANGWEI
 * @date    2026-07-04
 *
 * @note    不含平台 SDK；backend 由 linux_hw / sim_hw 在 bootstrap 阶段注入。
 *          tick() 须由调度层周期调用（与 hal_sensor 相同模式）。
 */

#ifndef ADAPTERS_HAL_GENERIC_HAL_VFD_H
#define ADAPTERS_HAL_GENERIC_HAL_VFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/ports/outbound/hal/hal_vfd_bind.h"
#include "framework/ports/outbound/hal/hal_vfd_port.h"
#include "framework/common/sw_error.h"

/** @brief  注册通用 VFD HAL 实现到 hal_vfd_port */
void hal_vfd_generic_register(void);

/**
 * @brief  绑定 VFD 实例 backend 与策略参数
 * @param  id   实例编号
 * @param  cfg  绑定配置；所有 backend 函数指针须非 NULL（has_rst_pin 可为 NULL）
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t hal_vfd_bind(hal_vfd_id_t id, const hal_vfd_bind_cfg_t *cfg);

/**
 * @brief  更新指定实例的通信监测掩码（不重置任何运行时状态）
 * @param  id    实例编号
 * @param  mask  新掩码（HAL_VFD_MON_* 组合）
 * @retval SW_OK / SW_ERR_NOT_INIT（id 未绑定）
 */
sw_err_t hal_vfd_set_monitor_mask(hal_vfd_id_t id, hal_vfd_monitor_mask_t mask);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_GENERIC_HAL_VFD_H */
