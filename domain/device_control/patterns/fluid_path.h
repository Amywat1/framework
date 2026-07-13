/**
 * @file    fluid_path.h
 * @brief   流体路径控制（路径掩码 + 引用计数 + 周期 tick）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    硬件输出通过 fluid_path_actuator_ops 注入；domain 不依赖 HAL。
 *          通道与路径均为无名字索引，机型命名由 projects 配置层定义。
 */

#ifndef DOMAIN_DEVICE_CONTROL_PATTERNS_FLUID_PATH_H
#define DOMAIN_DEVICE_CONTROL_PATTERNS_FLUID_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 执行器通道数上限（ref 表静态分配） */
#define FLUID_PATH_CHANNEL_MAX 16U
/** 逻辑路径数上限（受 fluid_path_mask_t 位宽限制） */
#define FLUID_PATH_PATH_MAX    32U

/** 执行器通道索引（无名字，由机型配置表赋值） */
typedef uint8_t fluid_path_channel_idx_t;

/** 执行器槽位类型 */
typedef enum {
    FLUID_PATH_SLOT_PUMP = 0,
    FLUID_PATH_SLOT_WATER_VALVE,
    FLUID_PATH_SLOT_AIR_VALVE,
    FLUID_PATH_SLOT_COUNT,
} fluid_path_slot_t;

/** 逻辑流体路径路径掩码（bit N 表示 path 索引 N 请求开启） */
typedef uint32_t fluid_path_mask_t;
#define FLUID_PATH_MASK(idx) (1u << (unsigned)(idx))

typedef struct {
    fluid_path_channel_idx_t ch;
    fluid_path_slot_t        slot;
} fluid_path_actuator_key_t;

typedef struct {
    uint8_t                          path_idx;
    const fluid_path_actuator_key_t *deps;
    uint8_t                          dep_count;
} fluid_path_def_t;

typedef sw_err_t (*fluid_path_slot_set_fn)(fluid_path_channel_idx_t ch, fluid_path_slot_t slot, bool on);

typedef struct {
    fluid_path_slot_set_fn slot_set;
    sw_err_t (*all_off)(void);
} fluid_path_actuator_ops_t;

typedef struct {
    uint32_t valve_open_delay_ms;
    uint32_t pump_stop_delay_ms;
    uint8_t  channel_count;
} fluid_path_cfg_t;

sw_err_t fluid_path_init(const fluid_path_cfg_t          *cfg,
                         const fluid_path_actuator_ops_t *ops,
                         const fluid_path_def_t          *paths,
                         size_t                           path_count);

sw_err_t fluid_path_set(fluid_path_mask_t target);
sw_err_t fluid_path_all_off(void);

/**
 * @brief  急停快速切断请求（无锁，由 fluid_path_poll 异步收敛）
 * @note   safety_thread 热路径专用，不持 s_mutex
 */
void fluid_path_emergency_off(void);

#ifdef FLUID_PATH_UNIT_TEST
void fluid_path_poll(uint64_t now_ms);
bool fluid_path_is_settled(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_PATTERNS_FLUID_PATH_H */
