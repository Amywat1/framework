/**
 * @file    water_channel.h
 * @brief   水路名称与执行器槽位（domain 语义，数值与 machine/HAL group×slot 对齐）
 * @author  HUWANGWEI
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
    WATER_CH_SHARED = 0,   /**< 全机共享（主泵等） */
    WATER_CH_CURTAIN,      /**< 水帘 */
    WATER_CH_FOAM,         /**< 泡沫 */
    WATER_CH_BRUSH,        /**< 侧刷冲水 */
    WATER_CH_HIGHPRES,     /**< 高压 */
    WATER_CH_COUNT,
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
