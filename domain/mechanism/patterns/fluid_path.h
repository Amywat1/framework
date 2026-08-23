/**
 * @file    fluid_path.h
 * @brief   流体路径控制（路径掩码 + 执行器对账 + 周期 tick）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    硬件输出通过 fluid_path_actuator_ops 注入；domain 不依赖 HAL。
 *          通道与路径均为无名字索引，机型命名由 projects 配置层定义。
 */

#ifndef DOMAIN_MECHANISM_PATTERNS_FLUID_PATH_H
#define DOMAIN_MECHANISM_PATTERNS_FLUID_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 执行器通道数上限（状态表静态分配） */
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
sw_err_t fluid_path_enable(fluid_path_mask_t mask);
sw_err_t fluid_path_disable(fluid_path_mask_t mask);
sw_err_t fluid_path_all_off(void);

/**
 * @brief  按当前目标对账一次阀泵输出
 * @param  now_ms  当前单调时钟毫秒数
 *
 * @note   领域层不自建线程：调用方须把本函数登记为周期任务驱动，
 *         推荐周期见 THD_FLUID_PATH_POLL_PERIOD_MS。不周期调用时
 *         fluid_path_set / enable / disable 请求会停留在 pending 而不生效。
 *         每拍按最新目标计算各执行器期望开关：先关泵、再开阀、再关阀、
 *         最后开泵。开阀/关泵延时只约束对应执行器，不阻塞其它路径的新指令。
 */
void fluid_path_poll(uint64_t now_ms);

/**
 * @brief  判断水路是否已收敛到目标状态
 * @retval true   已初始化、无强制关断、输出抑制未置位，且各执行器实际输出等于期望
 * @retval false  未初始化、强制关断、输出抑制有效，或输出尚未对齐目标
 */
bool fluid_path_is_settled(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_MECHANISM_PATTERNS_FLUID_PATH_H */
