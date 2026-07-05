/**
 * @file    hal_vfd_manager.h
 * @brief   VFD HAL 通用组合层（bind + 通信监测 + 复位脉冲编排）
 * @author  HUWANGWEI
 * @date    2026-07-04
 *
 * @note    不含平台 SDK；backend 由 providers/snack / sim 在 bootstrap 阶段注入。
 *          运行期推进由 vfd_manager poll 任务自管，不通过 hal_vfd_port 暴露给项目层。
 */

#ifndef ADAPTERS_HAL_COMPONENTS_VFD_MANAGER_HAL_VFD_MANAGER_H
#define ADAPTERS_HAL_COMPONENTS_VFD_MANAGER_HAL_VFD_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/adapters/outbound/hal/components/vfd_manager/hal_vfd_manager_bind.h"
#include "framework/ports/outbound/hal/hal_vfd_port.h"
#include "framework/common/sw_error.h"

/** @brief  注册通用 VFD HAL 实现到 hal_vfd_port */
void hal_vfd_manager_register(void);

/**
 * @brief  注册 VFD manager 周期推进任务。
 * @retval SW_OK 注册成功。
 * @retval SW_ERR_PARAM / SW_ERR_OVERFLOW 注册失败。
 * @note   任务由 scheduler_start_all() 统一启动；项目层只注册任务，不直接调用 tick。
 */
sw_err_t hal_vfd_manager_poll_register_task(void);

/**
 * @brief  绑定 VFD 实例 backend 与策略参数
 * @param  id   实例编号
 * @param  cfg  绑定配置；所有 backend 函数指针须非 NULL（has_rst_pin 可为 NULL）
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t hal_vfd_manager_bind(hal_vfd_id_t id, const hal_vfd_manager_bind_cfg_t *cfg);

/**
 * @brief  更新指定实例的通信监测掩码（不重置任何运行时状态）
 * @param  id    实例编号
 * @param  mask  新掩码（HAL_VFD_MON_* 组合）
 * @retval SW_OK / SW_ERR_NOT_INIT（id 未绑定）
 */
sw_err_t hal_vfd_manager_set_monitor_mask(hal_vfd_id_t id, hal_vfd_monitor_mask_t mask);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_COMPONENTS_VFD_MANAGER_HAL_VFD_MANAGER_H */
