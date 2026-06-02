/**
 * @file    m8_features.h
 * @brief   M8 机型功能安装开关
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    本文件定义哪些机械/通信功能已安装。
 *          未安装时相关报警自动降级为 NOTICE，不触发停机。
 *          硬件参数（总线、地址、串口等）见 m8_machine_config.h。
 */

#ifndef CONFIG_FEATURES_M8_FEATURES_H
#define CONFIG_FEATURES_M8_FEATURES_H

/* -------------------------------------------------------------------------
 * 刷子功能
 * ------------------------------------------------------------------------- */
#define CFG_BRUSH_TOP_INSTALLED         1   /* 顶刷：已安装 */
#define CFG_BRUSH_SIDE_INSTALLED        1   /* 侧刷（左右同控）：已安装 */
#define CFG_TOP_LIFT_INSTALLED          0   /* 顶刷升降步进：未安装 */

/* -------------------------------------------------------------------------
 * 水路功能
 * ------------------------------------------------------------------------- */
#define CFG_WATER_PUMP_INSTALLED        1   /* 水泵 */
#define CFG_WATER_CURTAIN_INSTALLED     1   /* 清水水帘 */
#define CFG_WATER_FOAM_INSTALLED        1   /* 泡沫 + 预洗液 */
#define CFG_WATER_BRUSH_INSTALLED       1   /* 侧刷冲水 */
#define CFG_WATER_HIGHPRES_INSTALLED    1   /* 高压冲洗 */

/* -------------------------------------------------------------------------
 * 入口设备
 * ------------------------------------------------------------------------- */
#define CFG_ENTRY_LIGHT_INSTALLED       1   /* 入口指示灯 */
#define CFG_ENTRY_ROD_INSTALLED         1   /* 电动推杆（入口挡杆）*/

/* -------------------------------------------------------------------------
 * 云端通信平台选择（只能选其一）
 * ------------------------------------------------------------------------- */
#define CFG_CLOUD_ALIYUN                1   /* 使用阿里云物联网平台 */
/* #define CFG_CLOUD_AWS                1 */

#endif /* CONFIG_FEATURES_M8_FEATURES_H */
