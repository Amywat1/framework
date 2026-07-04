/**
 * @file    m8_io_pins.h
 * @brief   M8 机型 IO 句柄常量（config 层，不依赖 drv_io.h）
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    引脚编号由 config/machine/m8_io_table.h 展开生成，与 drv_io 名称表同源。
 */

#ifndef CONFIG_MACHINE_M8_IO_PINS_H
#define CONFIG_MACHINE_M8_IO_PINS_H

#include "framework/common/io_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M8_IO_TABLE_DI(name, board, pin, desc) \
    static const io_di_t M8_IO_DI_##name = IO_DI((board), (pin));

#define DRV_IO_DI_DEF(name, board, pin, desc) M8_IO_TABLE_DI(name, board, pin, desc)
#include "projects/m8/config/m8_io_table.h"
#undef DRV_IO_DI_DEF
#undef M8_IO_TABLE_DI

#define M8_IO_TABLE_DO(name, board, pin, desc) \
    static const io_do_t M8_IO_DO_##name = IO_DO((board), (pin));

#define DRV_IO_DO_DEF(name, board, pin, desc) M8_IO_TABLE_DO(name, board, pin, desc)
#include "projects/m8/config/m8_io_table.h"
#undef DRV_IO_DO_DEF
#undef M8_IO_TABLE_DO

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_MACHINE_M8_IO_PINS_H */
