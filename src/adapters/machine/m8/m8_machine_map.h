/**
 * @file    m8_machine_map.h
 * @brief   M8 机型硬件映射（引脚别名 + 从 m8_machine_config.h 引入参数常量）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    仅供 adapters/hal/linux_hw/ 和 adapters/machine/m8/ 内部使用。
 *          domain/ 和 application/ 层不得直接包含此文件。
 *
 *          硬件参数（总线名、地址、脉冲宽度等）的唯一权威来源是
 *          config/machine/m8_machine_config.h。
 *          本文件只做两件事：
 *            1. 引入该配置文件（通过 src/ 根路径）
 *            2. 为适配器层提供 DO/DI 引脚的 M8_ 前缀别名
 */

#ifndef ADAPTERS_MACHINE_M8_MACHINE_MAP_H
#define ADAPTERS_MACHINE_M8_MACHINE_MAP_H

#include "config/machine/m8_machine_config.h"  /* 唯一参数来源 */
#include "driver/drv_io.h"                      /* drv_io_do_t / drv_io_di_t 枚举 */

/* -------------------------------------------------------------------------
 * IO 子板 CAN 总线参数 — 直接使用 CFG_* 宏（在此文件中作为 M8_ 别名）
 * ------------------------------------------------------------------------- */
#define M8_IO_CAN_BUS           CFG_IO_CAN_BUS
#define M8_IO_CAN_BAUD          CFG_IO_CAN_BAUD
#define M8_IO_SELF_NODE         CFG_IO_SELF_NODE
#define M8_IO_BOARD_COUNT       CFG_IO_BOARD_COUNT

/* -------------------------------------------------------------------------
 * VFD Modbus RTU 参数
 * ------------------------------------------------------------------------- */
#define M8_VFD_SERIAL_PORT      CFG_VFD_GANTRY_SERIAL_PORT  /* 刷子/龙门共用 */
#define M8_VFD_BAUD             CFG_VFD_GANTRY_BAUD
#define M8_VFD_BRUSH_ADDR       CFG_VFD_BRUSH_MODBUS_ADDR
#define M8_VFD_GANTRY_ADDR      CFG_VFD_GANTRY_MODBUS_ADDR

/* -------------------------------------------------------------------------
 * 步进电机脉冲参数
 * ------------------------------------------------------------------------- */
#define M8_STEPPER_PULSE_US     CFG_STEPPER_PULSE_US
#define M8_STEPPER_PULSE_BATCH  CFG_STEPPER_PULSE_BATCH
#define M8_LIFT_UP_MAX_PULSES   CFG_LIFT_UP_MAX_PULSES
#define M8_LIFT_DOWN_DEF_PULSES CFG_LIFT_DOWN_DEF_PULSES

/* -------------------------------------------------------------------------
 * 接触器切换等待时间
 * ------------------------------------------------------------------------- */
#define M8_BRUSH_CONTACTOR_WAIT_MS  CFG_BRUSH_CONTACTOR_WAIT_MS

/* -------------------------------------------------------------------------
 * 数字输出引脚别名（图纸 DO 编号 → drv_io 枚举，仅提升可读性）
 * ------------------------------------------------------------------------- */
#define M8_DO_ENTRY_GREEN1      DO_ENTRY_GREEN1
#define M8_DO_ENTRY_GREEN2      DO_ENTRY_GREEN2
#define M8_DO_ENTRY_RED         DO_ENTRY_RED
#define M8_DO_ENTRY_YELLOW      DO_ENTRY_YELLOW
#define M8_DO_ROD_EXTEND        DO_ROD_EXTEND
#define M8_DO_ROD_RETRACT       DO_ROD_RETRACT
#define M8_DO_WATER_PUMP        DO_WATER_PUMP
#define M8_DO_WATER_CURTAIN     DO_WATER_CURTAIN
#define M8_DO_WATER_FOAM        DO_WATER_FOAM
#define M8_DO_WATER_BRUSH       DO_WATER_BRUSH
#define M8_DO_WATER_HIGHPRES    DO_WATER_HIGHPRES
#define M8_DO_GANTRY_FWD        DO_GANTRY_FWD
#define M8_DO_GANTRY_REV        DO_GANTRY_REV
#define M8_DO_GANTRY_RST        DO_GANTRY_RST
#define M8_DO_BRUSH_FWD         DO_SIDE_BRUSH_FWD
#define M8_DO_BRUSH_RST         DO_SIDE_BRUSH_RST
#define M8_DO_TOP_BRUSH_ACT     DO_TOP_BRUSH_ACT    /* 接触器1：顶刷接 VFD */
#define M8_DO_SIDE_BRUSH_ACT    DO_SIDE_BRUSH_ACT   /* 接触器2：侧刷接 VFD */
#define M8_DO_LIFT_ENA          DO_TOP_LIFT_ENA
#define M8_DO_LIFT_DIR          DO_TOP_LIFT_DIR
#define M8_DO_LIFT_PUL          DO_TOP_LIFT_PUL

/* -------------------------------------------------------------------------
 * 数字输入引脚别名
 * ------------------------------------------------------------------------- */
#define M8_DI_GANTRY_FWD_LIM    DI_GANTRY_FWD_LIMIT
#define M8_DI_GANTRY_REV_LIM    DI_GANTRY_REAR_LIMIT
#define M8_DI_LIFT_UP_LIM       DI_TOP_LIFT_UP
#define M8_DI_LIFT_DOWN_LIM     DI_TOP_LIFT_DOWN
#define M8_DI_ENCODER           DI_ENCODER_PULSE
#define M8_DI_ESTOP             DI_ESTOP

#endif /* ADAPTERS_MACHINE_M8_MACHINE_MAP_H */
