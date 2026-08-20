/**
 * @file    hal_vfd_manager_bind.h
 * @brief   VFD 实例 backend 绑定配置（供 components/vfd_manager 与机型/平台注入）
 * @author  HUWANGWEI
 * @date    2026-07-04
 */

#ifndef ADAPTERS_OUTBOUND_HAL_COMPONENTS_VFD_MANAGER_HAL_VFD_MANAGER_BIND_H
#define ADAPTERS_OUTBOUND_HAL_COMPONENTS_VFD_MANAGER_HAL_VFD_MANAGER_BIND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/ports/outbound/hal/hal_vfd_port.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief components/vfd_manager 支持的最大实例槽位数 */
#define HAL_VFD_MANAGER_SLOT_MAX 8U

/** @brief 默认 RST 脉冲宽度（ms） */
#define HAL_VFD_DEFAULT_RST_PULSE_MS 200U

/** @brief 默认慢速监测轮询间隔（ms），故障码固定使用该值，电流未指定实例覆盖值时也使用该值 */
#define HAL_VFD_DEFAULT_MONITOR_PERIOD_MS 2000U

/** @brief 电流闭环控制场景下的快速采样间隔（ms），供需要电流反馈的实例覆盖使用 */
#define HAL_VFD_FAST_CURRENT_PERIOD_MS 150U

/**
 * @brief  通信监测项掩码（可按位组合）
 */
typedef uint8_t hal_vfd_monitor_mask_t;

#define HAL_VFD_MON_NONE    ((hal_vfd_monitor_mask_t)0x00U)
#define HAL_VFD_MON_FAULT   ((hal_vfd_monitor_mask_t)0x01U)
#define HAL_VFD_MON_CURRENT ((hal_vfd_monitor_mask_t)0x02U)
#define HAL_VFD_MON_ALL     ((hal_vfd_monitor_mask_t)0x03U)

typedef sw_err_t (*hal_vfd_backend_apply_gear_fn)(void *ctx, hal_vfd_gear_t gear);
typedef sw_err_t (*hal_vfd_backend_apply_frequency_fn)(void *ctx, hal_vfd_frequency_t frequency_centi_hz);
typedef sw_err_t (*hal_vfd_backend_init_fn)(void *ctx);
typedef sw_err_t (*hal_vfd_backend_stop_outputs_fn)(void *ctx);
typedef sw_err_t (*hal_vfd_backend_set_rst_fn)(void *ctx, bool level);
typedef sw_err_t (*hal_vfd_backend_read_fn)(void *ctx, hal_vfd_reg_t reg, uint16_t *p_val);
typedef sw_err_t (*hal_vfd_backend_clear_fault_fn)(void *ctx);
typedef hal_vfd_state_t (*hal_vfd_backend_get_state_fn)(void *ctx);
typedef bool (*hal_vfd_backend_has_rst_pin_fn)(void *ctx);
typedef bool (*hal_vfd_backend_has_clear_fault_fn)(void *ctx);

/**
 * @brief  VFD backend 原语契约；同一 provider 的所有实例共用一份 static const 单例
 * @note   has_rst_pin 可为 NULL（表示该 provider 不支持 RST 引脚，一律走 Modbus 清故障）
 */
typedef struct {
    /** @brief 初始化 backend 上下文；可为 NULL，表示无硬件初始化动作 */
    hal_vfd_backend_init_fn            init;
    hal_vfd_backend_apply_gear_fn      apply_gear;
    hal_vfd_backend_apply_frequency_fn apply_frequency;
    hal_vfd_backend_stop_outputs_fn    stop_outputs;
    hal_vfd_backend_set_rst_fn         set_rst;
    hal_vfd_backend_read_fn            read;
    /** @brief Modbus 清故障原语；可为 NULL，表示设备不支持。 */
    hal_vfd_backend_clear_fault_fn clear_fault;
    /** @brief 查询当前实例 profile 是否支持 Modbus 清故障；与 clear_fault 同时提供。 */
    hal_vfd_backend_has_clear_fault_fn has_clear_fault;
    hal_vfd_backend_get_state_fn       get_state;
    hal_vfd_backend_has_rst_pin_fn     has_rst_pin;
} hal_vfd_backend_ops_t;

/**
 * @brief  单 VFD 实例的 backend 单例引用 + 每实例上下文与策略参数
 */
typedef struct {
    const hal_vfd_backend_ops_t *ops;
    void                        *drv_ctx;

    uint32_t               rst_pulse_ms;
    uint32_t               fault_period_ms;
    uint32_t               current_period_ms;
    hal_vfd_monitor_mask_t monitor_mask;
} hal_vfd_manager_bind_cfg_t;

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_COMPONENTS_VFD_MANAGER_HAL_VFD_MANAGER_BIND_H */
