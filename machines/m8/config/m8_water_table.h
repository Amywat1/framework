/**
 * @file    m8_water_table.h
 * @brief   M8 水路绑定表（类型定义 + 编译期数据）
 * @author  HUWANGWEI
 * @date    2026-06-07
 *
 * @note    增删改水路 DO 映射只改本文件中的 m8_water_bind_table。
 *          channel = 水路名称，slot = 执行器类型，二者数值与 HAL group×slot 一致。
 */

#ifndef CONFIG_MACHINE_M8_WATER_TABLE_H
#define CONFIG_MACHINE_M8_WATER_TABLE_H

#include "domain/device/water_channel.h"
#include "machines/m8/config/m8_io_pins.h"
#include "common/io_handle.h"

/** 单行绑定：水路 + 槽位 → DO */
typedef struct
{
    water_channel_t channel;
    water_slot_t    slot;
    io_do_t         pin;
} m8_water_bind_row_t;

/* -------------------------------------------------------------------------
 * 绑定表（增删改水路 DO 映射只改此处）
 * ------------------------------------------------------------------------- */
static const m8_water_bind_row_t m8_water_bind_table[] = {
    { WCH_SHARED,   WATER_SLOT_PUMP,        M8_IO_DO_WATER_PUMP     },
    { WCH_CURTAIN,  WATER_SLOT_WATER_VALVE, M8_IO_DO_WATER_CURTAIN  },
    { WCH_FOAM,     WATER_SLOT_WATER_VALVE, M8_IO_DO_WATER_FOAM     },
    { WCH_BRUSH,    WATER_SLOT_WATER_VALVE, M8_IO_DO_WATER_BRUSH    },
    { WCH_HIGHPRES, WATER_SLOT_WATER_VALVE, M8_IO_DO_WATER_HIGHPRES },
};

#define M8_WATER_BIND_TABLE_COUNT \
    ((unsigned)(sizeof(m8_water_bind_table) / sizeof(m8_water_bind_table[0])))

#endif /* CONFIG_MACHINE_M8_WATER_TABLE_H */
