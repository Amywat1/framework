/**
 * @file    m8_signal_table.h
 * @brief   M8 DI 信号滤波配置表（编译期只读）
 * @author  HUWANGWEI
 * @date    2026-04-13
 *
 * @note    每行定义一路 DI 的 IO 绑定、极性与防抖参数；不含报警语义。
 *          DI 句柄复用 config/machine/m8_io_pins.h（与 m8_io_table.h 同源）。
 */

#ifndef CONFIG_MACHINE_M8_SIGNAL_TABLE_H
#define CONFIG_MACHINE_M8_SIGNAL_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "common/sw_types.h"
#include "common/io_handle.h"
#include "config/machine/m8_io_pins.h"

/* -------------------------------------------------------------------------
 * 信号标识
 * ------------------------------------------------------------------------- */
typedef enum
{
    M8_SIG_ESTOP = 0,            /* 急停 */
    M8_SIG_GANTRY_FWD_LIM,       /* 龙门前限位 */
    M8_SIG_GANTRY_REV_LIM,       /* 龙门后限位 */
    M8_SIG_LIFT_UP_LIM,          /* 顶刷升降上限位 */
    M8_SIG_LIFT_DOWN_LIM,        /* 顶刷升降下限位 */
    M8_SIG_SIDE_BRUSH_OVERLOAD,  /* 侧刷过载（报警源）*/
    M8_SIG_FAN_ALARM,            /* 风机报警反馈（报警源）*/
    M8_SIG_REAR_LOCK_HOME,       /* 后轮锁紧机构原点 */
    M8_SIG_MAX
} m8_signal_id_t;

/* -------------------------------------------------------------------------
 * 单路信号配置（行下标即为对应的 m8_signal_id_t 枚举值）
 * ------------------------------------------------------------------------- */
typedef struct
{
    io_di_t  io_id;          /**< DI 句柄 */
    bool     active_low;     /**< true=低电平有效（常闭接法） */
    uint8_t  trig_count;     /**< 触发确认次数 */
    uint8_t  release_count;  /**< 释放确认次数 */
} m8_signal_cfg_t;

/* -------------------------------------------------------------------------
 * 配置表（定义在 m8_signal_table.c，行下标与 m8_signal_id_t 枚举值严格对应）
 * ------------------------------------------------------------------------- */
extern const m8_signal_cfg_t m8_signal_table[M8_SIG_MAX];

#define M8_SIGNAL_TABLE_SIZE  ((int)M8_SIG_MAX)

#endif /* CONFIG_MACHINE_M8_SIGNAL_TABLE_H */
