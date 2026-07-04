/**
 * @file    hal_vfd_manager_bind.h
 * @brief   VFD 实例 backend 绑定配置（ports 层，供 components/vfd_manager 与机型/平台注入）
 * @author  HUWANGWEI
 * @date    2026-07-04
 *
 * @note    组合层 hal_vfd 通过本结构体注入平台原语回调；
 *          机型 setup 在 bootstrap 阶段调用 hal_vfd_manager_bind() 完成实例绑定。
 */

#ifndef ADAPTERS_HAL_COMPONENTS_VFD_MANAGER_HAL_VFD_MANAGER_BIND_H
#define ADAPTERS_HAL_COMPONENTS_VFD_MANAGER_HAL_VFD_MANAGER_BIND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"
#include "framework/common/vfd_types.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief components/vfd_manager 支持的最大实例槽位数 */
#define HAL_VFD_MANAGER_SLOT_MAX  8U

/** @brief 默认 RST 脉冲宽度（ms） */
#define HAL_VFD_DEFAULT_RST_PULSE_MS       200U

/** @brief 默认慢速监测轮询间隔（ms） */
#define HAL_VFD_DEFAULT_MONITOR_PERIOD_MS    2000U

/**
 * @brief  通信监测项掩码（可按位组合）
 */
typedef uint8_t hal_vfd_monitor_mask_t;

#define HAL_VFD_MON_NONE     ((hal_vfd_monitor_mask_t)0x00U)
#define HAL_VFD_MON_FAULT    ((hal_vfd_monitor_mask_t)0x01U)
#define HAL_VFD_MON_CURRENT  ((hal_vfd_monitor_mask_t)0x02U)
#define HAL_VFD_MON_ALL      ((hal_vfd_monitor_mask_t)0x03U)

typedef sw_err_t (*hal_vfd_backend_apply_gear_fn)(void *ctx, hal_vfd_gear_t gear);
typedef sw_err_t (*hal_vfd_backend_stop_outputs_fn)(void *ctx);
typedef sw_err_t (*hal_vfd_backend_set_rst_fn)(void *ctx, bool level);
typedef sw_err_t (*hal_vfd_backend_read_fn)(void *ctx, hal_vfd_reg_t reg, uint16_t *p_val);
typedef sw_err_t (*hal_vfd_backend_write_fn)(void *ctx, hal_vfd_reg_t reg, uint16_t val);
typedef hal_vfd_state_t (*hal_vfd_backend_get_state_fn)(void *ctx);
typedef bool (*hal_vfd_backend_has_rst_pin_fn)(void *ctx);

/**
 * @brief  单 VFD 实例 backend 与策略参数
 */
typedef struct
{
    void                            *drv_ctx;
    hal_vfd_backend_apply_gear_fn    apply_gear;
    hal_vfd_backend_stop_outputs_fn  stop_outputs;
    hal_vfd_backend_set_rst_fn       set_rst;
    hal_vfd_backend_read_fn          read;
    hal_vfd_backend_write_fn         write;
    hal_vfd_backend_get_state_fn     get_state;
    hal_vfd_backend_has_rst_pin_fn   has_rst_pin;

    uint32_t               rst_pulse_ms;
    uint32_t               monitor_period_ms;
    hal_vfd_monitor_mask_t monitor_mask;
} hal_vfd_manager_bind_cfg_t;

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_COMPONENTS_VFD_MANAGER_HAL_VFD_MANAGER_BIND_H */
