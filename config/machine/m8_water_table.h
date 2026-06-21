/**
 * @file    m8_water_table.h
 * @brief   M8 水路绑定行类型定义
 * @author  HUWANGWEI
 * @date    2026-06-07
 *
 * @note    增删改水路 DO 映射只改 m8_water_setup.c 中的 s_m8_water_bind_table。
 *          channel = 水路名称，slot = 执行器类型，二者数值与 HAL group×slot 一致。
 */

#ifndef CONFIG_MACHINE_M8_WATER_TABLE_H
#define CONFIG_MACHINE_M8_WATER_TABLE_H

#include "domain/device/water_channel.h"
#include "config/machine/m8_io_pins.h"
#include "common/io_handle.h"

/** 单行绑定：水路 + 槽位 → DO */
typedef struct
{
    water_channel_t channel;
    water_slot_t    slot;
    io_do_t         pin;
} m8_water_bind_row_t;

#endif /* CONFIG_MACHINE_M8_WATER_TABLE_H */
