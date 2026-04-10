/**
 * @file    m8_feature_map.h
 * @brief   M8 机型硬件功能安装矩阵
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    1 = 已安装，0 = 未安装。
 *          未安装时对应报警降级为 NOTICE（不停机），由 alarm_core 处理。
 */

#ifndef ADAPTERS_MACHINE_M8_FEATURE_MAP_H
#define ADAPTERS_MACHINE_M8_FEATURE_MAP_H

/* -------------------------------------------------------------------------
 * 刷子与升降
 * ------------------------------------------------------------------------- */
#define M8_FEAT_BRUSH_TOP_INSTALLED      1  /* 顶刷 */
#define M8_FEAT_BRUSH_SIDE_INSTALLED     1  /* 侧刷 */
#define M8_FEAT_TOP_LIFT_INSTALLED       1  /* 顶刷升降 */

/* -------------------------------------------------------------------------
 * 水路
 * ------------------------------------------------------------------------- */
#define M8_FEAT_WATER_PUMP_INSTALLED     1
#define M8_FEAT_WATER_CURTAIN_INSTALLED  1  /* 清水水帘 */
#define M8_FEAT_WATER_FOAM_INSTALLED     1  /* 泡沫 */
#define M8_FEAT_WATER_BRUSH_INSTALLED    1  /* 刷子冲水 */
#define M8_FEAT_WATER_HIGHPRES_INSTALLED 1  /* 高压冲洗 */

/* -------------------------------------------------------------------------
 * 入口设备
 * ------------------------------------------------------------------------- */
#define M8_FEAT_ENTRY_LIGHT_INSTALLED    1  /* 入口指示灯 */
#define M8_FEAT_ENTRY_ROD_INSTALLED      1  /* 电动推杆 */

/* -------------------------------------------------------------------------
 * 云端
 * ------------------------------------------------------------------------- */
#define M8_FEAT_CLOUD_ALIYUN             1  /* 阿里云 MQTT */

#endif /* ADAPTERS_MACHINE_M8_FEATURE_MAP_H */
