/**
 * @file    water_channel.h
 * @brief   水路名称与执行器槽位（domain 语义，数值与 machine/HAL group×slot 对齐）
 * @author  胡望伟
 * @date    2026-06-07
 */

#ifndef DOMAIN_DEVICE_WATER_CHANNEL_H
#define DOMAIN_DEVICE_WATER_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

/** 水路名称（同时作为 HAL do_group 编号） */
typedef enum
{
    WCH_SHARED = 0,   /**< 全机共享（主泵等） */
    WCH_CURTAIN,      /**< 水帘 */
    WCH_FOAM,         /**< 泡沫 */
    WCH_BRUSH,        /**< 侧刷冲水 */
    WCH_HIGHPRES,     /**< 高压 */
    WCH_COUNT,
} water_channel_t;

/** 水路内执行器槽位（同时作为 HAL do_slot 编号） */
typedef enum
{
    WATER_SLOT_PUMP = 0,        /**< 主液泵 / 共享泵 */
    WATER_SLOT_CHEM_PUMP,       /**< 药剂泵 */
    WATER_SLOT_WATER_VALVE,     /**< 水阀 */
    WATER_SLOT_AIR_VALVE,       /**< 气阀 */
    WATER_SLOT_COUNT,
} water_slot_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_WATER_CHANNEL_H */
