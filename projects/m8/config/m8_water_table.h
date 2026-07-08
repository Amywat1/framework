/**
 * @file    m8_water_table.h
 * @brief   M8 水路配置（命名 + 路径拓扑 + 执行器引脚）
 * @author  HUWANGWEI
 * @date    2026-06-07
 */

#ifndef CONFIG_MACHINE_M8_WATER_TABLE_H
#define CONFIG_MACHINE_M8_WATER_TABLE_H

#include "framework/domain/device_control/mechanism/water.h"
#include "projects/m8/config/m8_io_pins.h"
#include "framework/common/io_handle.h"

/* -------------------------------------------------------------------------
 * 路径与通道命名
 * ------------------------------------------------------------------------- */
typedef enum
{
    M8_WATER_CH_SHARED = 0,
    M8_WATER_CH_CURTAIN,
    M8_WATER_CH_FOAM,
    M8_WATER_CH_BRUSH,
    M8_WATER_CH_HIGHPRES,
    M8_WATER_CH_BOTTOM_FOAM,
    M8_WATER_CH_COUNT,
} m8_water_channel_t;

typedef enum
{
    M8_WATER_PATH_CURTAIN = 0,
    M8_WATER_PATH_FOAM,
    M8_WATER_PATH_BOTTOM_FOAM,
    M8_WATER_PATH_BRUSH,
    M8_WATER_PATH_HIGHPRES,
    M8_WATER_PATH_COUNT,
} m8_water_path_id_t;

#define M8_WATER_PATH_MASK(id)  WATER_PATH_MASK(id)

/* -------------------------------------------------------------------------
 * 执行器引脚表（channel + slot → DO）
 * ------------------------------------------------------------------------- */
typedef struct
{
    water_channel_idx_t channel;
    water_slot_t        slot;
    io_do_t             pin;
} m8_water_actuator_row_t;

static const m8_water_actuator_row_t m8_water_actuator_table[] = {
    { M8_WATER_CH_SHARED,      WATER_SLOT_PUMP,        M8_IO_DO_WATER_PUMP },
    { M8_WATER_CH_CURTAIN,     WATER_SLOT_WATER_VALVE, M8_IO_DO_WATER_CURTAIN },
    { M8_WATER_CH_FOAM,        WATER_SLOT_WATER_VALVE, M8_IO_DO_WATER_TOP_FOAM },
    { M8_WATER_CH_BRUSH,       WATER_SLOT_WATER_VALVE, M8_IO_DO_WATER_TOP },
    { M8_WATER_CH_HIGHPRES,    WATER_SLOT_WATER_VALVE, M8_IO_DO_WATER_BUTTOM },
    { M8_WATER_CH_BOTTOM_FOAM, WATER_SLOT_WATER_VALVE, M8_IO_DO_WATER_BUTTOM_FOAM },
};

#define M8_WATER_ACTUATOR_TABLE_COUNT \
    ((unsigned)(sizeof(m8_water_actuator_table) / sizeof(m8_water_actuator_table[0])))

/* -------------------------------------------------------------------------
 * 路径拓扑表（path → 依赖执行器）
 * ------------------------------------------------------------------------- */
static const water_actuator_key_t m8_deps_curtain[] = {
    { M8_WATER_CH_CURTAIN, WATER_SLOT_WATER_VALVE },
    { M8_WATER_CH_SHARED,  WATER_SLOT_PUMP },
};
static const water_actuator_key_t m8_deps_foam[] = {
    { M8_WATER_CH_FOAM,   WATER_SLOT_WATER_VALVE },
    { M8_WATER_CH_SHARED, WATER_SLOT_PUMP },
};
static const water_actuator_key_t m8_deps_bottom_foam[] = {
    { M8_WATER_CH_BOTTOM_FOAM, WATER_SLOT_WATER_VALVE },
    { M8_WATER_CH_SHARED,      WATER_SLOT_PUMP },
};
static const water_actuator_key_t m8_deps_brush[] = {
    { M8_WATER_CH_BRUSH,  WATER_SLOT_WATER_VALVE },
    { M8_WATER_CH_SHARED, WATER_SLOT_PUMP },
};
static const water_actuator_key_t m8_deps_highpres[] = {
    { M8_WATER_CH_HIGHPRES, WATER_SLOT_WATER_VALVE },
    { M8_WATER_CH_SHARED,   WATER_SLOT_PUMP },
};

static const water_path_def_t m8_water_path_table[] = {
    { M8_WATER_PATH_CURTAIN,     m8_deps_curtain,     2U },
    { M8_WATER_PATH_FOAM,        m8_deps_foam,        2U },
    { M8_WATER_PATH_BOTTOM_FOAM, m8_deps_bottom_foam, 2U },
    { M8_WATER_PATH_BRUSH,       m8_deps_brush,       2U },
    { M8_WATER_PATH_HIGHPRES,    m8_deps_highpres,    2U },
};

#define M8_WATER_PATH_TABLE_COUNT \
    ((unsigned)(sizeof(m8_water_path_table) / sizeof(m8_water_path_table[0])))

#endif /* CONFIG_MACHINE_M8_WATER_TABLE_H */
