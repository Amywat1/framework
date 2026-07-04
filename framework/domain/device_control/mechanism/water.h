/**
 * @file    water.h
 * @brief   水路设备接口（泵 / 水帘 / 泡沫 / 刷子冲水 / 高压）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    硬件输出通过 water_actuator_ops 注入，domain 不依赖 HAL。
 */

#ifndef DOMAIN_DEVICE_WATER_H
#define DOMAIN_DEVICE_WATER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/device_control/mechanism/water_channel.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

/** 水路执行器输出回调（由 machine 适配层实现） */
typedef sw_err_t (*water_slot_set_fn)(water_channel_t ch, water_slot_t slot, bool on);

typedef struct
{
    water_slot_set_fn slot_set;
    sw_err_t        (*all_off)(void); /**< 可选；关闭全部已绑定 DO（对应 hal_do_group.all_off）*/
} water_actuator_ops_t;

/** 水路时序配置（由 machine 适配层注入，domain 不依赖机型常量） */
typedef struct
{
    uint32_t valve_open_delay_ms;  /**< 开阀后等待阀体到位再开泵（ms）*/
    uint32_t pump_stop_delay_ms;   /**< 关泵后等待管路泄压再关阀（ms）*/
} water_cfg_t;

/**
 * @brief  初始化水路组件并注入时序配置与执行器（关闭所有水路）
 * @param  cfg  时序配置，不可为 NULL
 * @param  ops  执行器操作表，slot_set 不可为 NULL
 */
sw_err_t water_init(const water_cfg_t *cfg, const water_actuator_ops_t *ops);

/**
 * @brief  开启预洗（先开泡沫阀和水帘阀，延时后开泵）
 */
sw_err_t water_prewash_on(void);

/**
 * @brief  关闭预洗（关泡沫阀和水帘阀）
 * @note   若关闭后已无其他水阀开启，则自动停泵并等待管路泄压。
 */
sw_err_t water_prewash_off(void);

/**
 * @brief  开启刷子冲水（先开刷子水阀，延时后开泵）
 */
sw_err_t water_brush_on(void);

/**
 * @brief  关闭刷子冲水
 * @note   若关闭后已无其他水阀开启，则自动停泵并等待管路泄压。
 */
sw_err_t water_brush_off(void);

/**
 * @brief  开启高压冲洗（先开高压阀，延时后开泵）
 */
sw_err_t water_highpres_on(void);

/**
 * @brief  关闭高压冲洗
 * @note   若关闭后已无其他水阀开启，则自动停泵并等待管路泄压。
 */
sw_err_t water_highpres_off(void);

/**
 * @brief  关闭所有水路（先停泵并等待管路泄压，再关全部水阀）
 */
sw_err_t water_all_off(void);

/**
 * @brief  查询水泵是否处于运行状态
 * @note   此接口为只读查询，可由报警轮询线程并发调用。
 * @return true 表示水泵运行中
 */
bool water_is_pump_on(void);

/**
 * @brief  查询是否有任何水阀处于打开状态
 * @note   此接口为只读查询，可由报警轮询线程并发调用。
 * @return true 表示至少一个水阀已开启
 */
bool water_is_any_valve_open(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_WATER_H */
