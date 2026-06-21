/**
 * @file    m8_feature_map.h
 * @brief   M8 机型功能安装矩阵（从 m8_features.h 引入，不重复定义）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    功能开关的唯一权威来源是 config/features/m8_features.h。
 *          本文件仅提供 M8_FEAT_* 前缀别名，供适配器层使用。
 *          domain/ 和 application/ 层不得直接包含此文件。
 */

#ifndef ADAPTERS_MACHINE_M8_FEATURE_MAP_H
#define ADAPTERS_MACHINE_M8_FEATURE_MAP_H

#include "config/features/m8_features.h"   /* 唯一功能开关来源 */

/* -------------------------------------------------------------------------
 * 刷子与升降（M8_ 前缀别名）
 * ------------------------------------------------------------------------- */
#define M8_FEAT_BRUSH_TOP_INSTALLED      CFG_BRUSH_TOP_INSTALLED
#define M8_FEAT_BRUSH_SIDE_INSTALLED     CFG_BRUSH_SIDE_INSTALLED
#define M8_FEAT_TOP_LIFT_INSTALLED       CFG_TOP_LIFT_INSTALLED

/* -------------------------------------------------------------------------
 * 水路
 * ------------------------------------------------------------------------- */
#define M8_FEAT_WATER_PUMP_INSTALLED     CFG_WATER_PUMP_INSTALLED
#define M8_FEAT_WATER_CURTAIN_INSTALLED  CFG_WATER_CURTAIN_INSTALLED
#define M8_FEAT_WATER_FOAM_INSTALLED     CFG_WATER_FOAM_INSTALLED
#define M8_FEAT_WATER_BRUSH_INSTALLED    CFG_WATER_BRUSH_INSTALLED
#define M8_FEAT_WATER_HIGHPRES_INSTALLED CFG_WATER_HIGHPRES_INSTALLED

/* -------------------------------------------------------------------------
 * 入口设备
 * ------------------------------------------------------------------------- */
#define M8_FEAT_ENTRY_LIGHT_INSTALLED    CFG_ENTRY_LIGHT_INSTALLED
#define M8_FEAT_ENTRY_ROD_INSTALLED      CFG_ENTRY_ROD_INSTALLED

/* -------------------------------------------------------------------------
 * 云端
 * ------------------------------------------------------------------------- */
#define M8_FEAT_CLOUD_ALIYUN             CFG_CLOUD_ALIYUN

#endif /* ADAPTERS_MACHINE_M8_FEATURE_MAP_H */
