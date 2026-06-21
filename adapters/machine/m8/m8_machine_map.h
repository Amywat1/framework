/**
 * @file    m8_machine_map.h
 * @brief   M8 机型硬件映射（引脚别名 + 从 m8_machine_config.h 引入参数常量）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    仅供 adapters/machine/m8/ 及同类机型 HAL 适配器内部使用。
 *          domain/ 和 application/ 层不得直接包含此文件。
 *
 *          硬件参数（总线名、地址、脉冲宽度等）的唯一权威来源是
 *          config/machine/m8_machine_config.h。
 *          IO 句柄来自 config/machine/m8_io_pins.h（与 m8_io_table.h 同源）。
 *          本文件为适配器层提供 M8_ 前缀别名。
 */

#ifndef ADAPTERS_MACHINE_M8_MACHINE_MAP_H
#define ADAPTERS_MACHINE_M8_MACHINE_MAP_H

#include "config/machine/m8_machine_config.h"
#include "config/machine/m8_io_config.h"
#include "config/machine/m8_io_pins.h"

/* -------------------------------------------------------------------------
 * IO 子板 CAN 总线参数 — 直接使用 CFG_* 宏（在此文件中作为 M8_ 别名）
 * ------------------------------------------------------------------------- */
#define M8_IO_CAN_BUS           CFG_IO_CAN_BUS
#define M8_IO_CAN_BAUD          CFG_IO_CAN_BAUD
#define M8_IO_SELF_NODE         CFG_IO_SELF_NODE
#define M8_IO_BOARD_COUNT       CFG_IO_BOARD_COUNT

/* -------------------------------------------------------------------------
 * 接触器切换等待时间
 * ------------------------------------------------------------------------- */
#define M8_BRUSH_CONTACTOR_WAIT_MS  CFG_BRUSH_CONTACTOR_WAIT_MS

/* -------------------------------------------------------------------------
 * 数字输出引脚别名
 * ------------------------------------------------------------------------- */
#define M8_DO_ENTRY_GREEN1      M8_IO_DO_ENTRY_GREEN1
#define M8_DO_ENTRY_GREEN2      M8_IO_DO_ENTRY_GREEN2
#define M8_DO_ENTRY_RED         M8_IO_DO_ENTRY_RED
#define M8_DO_ENTRY_YELLOW      M8_IO_DO_ENTRY_YELLOW
#define M8_DO_ROD_EXTEND        M8_IO_DO_ROD_EXTEND
#define M8_DO_ROD_RETRACT       M8_IO_DO_ROD_RETRACT
#define M8_DO_WATER_PUMP        M8_IO_DO_WATER_PUMP
#define M8_DO_WATER_CURTAIN     M8_IO_DO_WATER_CURTAIN
#define M8_DO_WATER_FOAM        M8_IO_DO_WATER_FOAM
#define M8_DO_WATER_BRUSH       M8_IO_DO_WATER_BRUSH
#define M8_DO_WATER_HIGHPRES    M8_IO_DO_WATER_HIGHPRES
#define M8_DO_GANTRY_FWD        M8_IO_DO_GANTRY_FWD
#define M8_DO_GANTRY_REV        M8_IO_DO_GANTRY_REV
#define M8_DO_GANTRY_RST        M8_IO_DO_GANTRY_RST
#define M8_DO_BRUSH_FWD         M8_IO_DO_SIDE_BRUSH_FWD
#define M8_DO_BRUSH_RST         M8_IO_DO_SIDE_BRUSH_RST
#define M8_DO_TOP_BRUSH_ACT     M8_IO_DO_TOP_BRUSH_ACT
#define M8_DO_SIDE_BRUSH_ACT    M8_IO_DO_SIDE_BRUSH_ACT

/* -------------------------------------------------------------------------
 * 数字输入引脚别名
 * ------------------------------------------------------------------------- */
#define M8_DI_GANTRY_FWD_LIM    M8_IO_DI_GANTRY_FWD_LIMIT
#define M8_DI_GANTRY_REV_LIM    M8_IO_DI_GANTRY_REV_LIMIT
#define M8_DI_LIFT_UP_LIM       M8_IO_DI_LIFT_UP_LIMIT
#define M8_DI_LIFT_DOWN_LIM     M8_IO_DI_LIFT_DOWN_LIMIT
#define M8_DI_ENCODER           M8_IO_DI_GANTRY_ENCODER_PULSE
#define M8_DI_ESTOP             M8_IO_DI_ESTOP

#endif /* ADAPTERS_MACHINE_M8_MACHINE_MAP_H */
