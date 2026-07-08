/**
 * @file    water.h
 * @brief   水路控制（路径掩码 + 引用计数 + 异步 worker）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    硬件输出通过 water_actuator_ops 注入；domain 不依赖 HAL。
 *          通道与路径均为无名字索引，机型命名由 projects 配置层定义。
 */

#ifndef DOMAIN_DEVICE_WATER_H
#define DOMAIN_DEVICE_WATER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 执行器通道数上限（ref 表静态分配） */
#define WATER_CHANNEL_MAX  16U

/** 执行器通道索引（无名字，由机型配置表赋值） */
typedef uint8_t water_channel_idx_t;

/** 执行器槽位类型 */
typedef enum
{
    WATER_SLOT_PUMP = 0,
    WATER_SLOT_CHEM_PUMP,
    WATER_SLOT_WATER_VALVE,
    WATER_SLOT_AIR_VALVE,
    WATER_SLOT_COUNT,
} water_slot_t;

/** 逻辑水路路径掩码（bit N 表示 path 索引 N 请求开启） */
typedef uint32_t water_path_mask_t;
#define WATER_PATH_MASK(idx) (1u << (unsigned)(idx))

typedef struct
{
    water_channel_idx_t ch;
    water_slot_t        slot;
} water_actuator_key_t;

typedef struct
{
    uint8_t                      path_idx;
    const water_actuator_key_t *deps;
    uint8_t                      dep_count;
} water_path_def_t;

typedef sw_err_t (*water_slot_set_fn)(water_channel_idx_t ch, water_slot_t slot, bool on);

typedef struct
{
    water_slot_set_fn slot_set;
    sw_err_t        (*all_off)(void);
} water_actuator_ops_t;

typedef struct
{
    uint32_t             valve_open_delay_ms;
    uint32_t             pump_stop_delay_ms;
    uint8_t              channel_count;
    water_actuator_key_t main_pump;
} water_cfg_t;

sw_err_t water_init(const water_cfg_t *cfg,
                    const water_actuator_ops_t *ops,
                    const water_path_def_t *paths,
                    size_t path_count);

sw_err_t water_path_set(water_path_mask_t target);
sw_err_t water_all_off(void);
bool     water_is_pump_on(void);
bool     water_is_any_valve_open(void);

#ifdef WATER_UNIT_TEST
void water_poll(uint32_t now_ms);
bool water_is_settled(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_WATER_H */
