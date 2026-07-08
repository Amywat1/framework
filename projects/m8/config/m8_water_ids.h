/**
 * @file    m8_water_ids.h
 * @brief   M8 水路通道与路径命名（机型专属）
 * @author  HUWANGWEI
 * @date    2026-06-07
 */

#ifndef CONFIG_MACHINE_M8_WATER_IDS_H
#define CONFIG_MACHINE_M8_WATER_IDS_H

#include "framework/domain/device_control/mechanism/water.h"

/** M8 执行器通道 */
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

/** M8 逻辑水路路径 */
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

#endif /* CONFIG_MACHINE_M8_WATER_IDS_H */
