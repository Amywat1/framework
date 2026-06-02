/**
 * @file    m8_io_pins.h
 * @brief   M8 机型 IO 句柄常量（config 层，不依赖 drv_io.h）
 * @author  胡望伟
 * @date    2026-06-01
 *
 * @note    引脚编号与 config/machine/m8_io_table.h 一致，供 motor/vfd 等配置表使用。
 */

#ifndef CONFIG_MACHINE_M8_IO_PINS_H
#define CONFIG_MACHINE_M8_IO_PINS_H

#include "common/io_handle.h"

/* DI */
#define M8_IO_DI_GANTRY_ENCODER_PULSE   IO_DI(1U, 12U)
#define M8_IO_DI_GANTRY_FWD_LIM         IO_DI(1U, 13U)
#define M8_IO_DI_GANTRY_REV_LIM         IO_DI(1U, 14U)

/* DO */
#define M8_IO_DO_SIDE_BRUSH_FWD         IO_DO(1U, 11U)
#define M8_IO_DO_SIDE_BRUSH_REV         IO_DO(1U, 12U)
#define M8_IO_DO_SIDE_BRUSH_RST         IO_DO(1U, 13U)
#define M8_IO_DO_GANTRY_FWD             IO_DO(1U, 14U)
#define M8_IO_DO_GANTRY_REV             IO_DO(1U, 15U)
#define M8_IO_DO_GANTRY_RST             IO_DO(1U, 16U)

#endif /* CONFIG_MACHINE_M8_IO_PINS_H */
