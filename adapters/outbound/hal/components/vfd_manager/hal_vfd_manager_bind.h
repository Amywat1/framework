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

/** @brief RST 脉冲推进周期（ms） */
#define HAL_VFD_PULSE_PERIOD_MS 20U

/** @brief 监测调度切片（ms），每拍最多一笔总线 */
#define HAL_VFD_MONITOR_SLICE_MS 20U

/** @brief 连续快采达到该次数后穿插一笔到期后台通道 */
#define HAL_VFD_FAST_KEEPALIVE_EVERY 8U

/** @brief 快采间隔未达标告警的最小间隔（ms） */
#define HAL_VFD_FAST_LAG_WARN_INTERVAL_MS 2000U

/**
 * @brief  单通道采样服务等级
 */
typedef enum {
    HAL_VFD_SAMPLE_OFF = 0,    /**< 不读 */
    HAL_VFD_SAMPLE_BACKGROUND, /**< 后台：按 period 到期后轮询，可被快采穿插保活 */
    HAL_VFD_SAMPLE_FAST,       /**< 快采：运行中优先，多通道轮询分享总线 */
} hal_vfd_sample_class_t;

/**
 * @brief  单通道采样策略
 * @note   OFF 时 period_ms 必须为 0；BACKGROUND / FAST 时 period_ms 必须大于 0。
 */
typedef struct {
    hal_vfd_sample_class_t class;     /**< 服务等级 */
    uint32_t               period_ms; /**< 目标周期（ms） */
} hal_vfd_channel_policy_t;

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

    uint32_t rst_pulse_ms;

    /** @brief 故障码通道策略 */
    hal_vfd_channel_policy_t fault;
    /** @brief 电流通道策略 */
    hal_vfd_channel_policy_t current;
} hal_vfd_manager_bind_cfg_t;

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_COMPONENTS_VFD_MANAGER_HAL_VFD_MANAGER_BIND_H */
