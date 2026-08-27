/**
 * @file    io_sample.h
 * @brief   数字输入采样值与质量定义
 */

#ifndef COMMON_IO_SAMPLE_H
#define COMMON_IO_SAMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief 数字输入快照的最长有效时间，超过后质量标记为 STALE。 */
#define IO_INPUT_FRESHNESS_TIMEOUT_MS 100U

/** @brief ADC 快照的最长有效时间，超过后质量标记为 STALE。 */
#define IO_ADC_FRESHNESS_TIMEOUT_MS 100U

/** @brief 脉冲计数快照的最长有效时间，超过后质量标记为 STALE。 */
#define IO_PULSE_FRESHNESS_TIMEOUT_MS 100U

/** @brief adc_read / adc_mv / adc_ma 在尚无有效快照时的返回值。 */
#define IO_ADC_ERR_UNINITIALIZED (-99)

/** @brief pulse_read 在尚无有效快照时的返回值。 */
#define IO_PULSE_ERR_UNINITIALIZED (-99)

/** @brief 硬件脉冲计数溢出/无效哨兵（与 Snack SDK 约定一致）。 */
#define IO_PULSE_INVALID_COUNT 0x0FFFFFFF

/** @brief 数字输入采样质量。 */
typedef enum {
    IO_SAMPLE_QUALITY_UNINITIALIZED = 0, /**< IO 驱动尚未初始化。 */
    IO_SAMPLE_QUALITY_PROBING,           /**< 正在探测子板，尚无可信采样。 */
    IO_SAMPLE_QUALITY_VALID,             /**< 电平来自当前有效采样。 */
    IO_SAMPLE_QUALITY_STALE,             /**< 保留最后电平，但已超过有效期。 */
    IO_SAMPLE_QUALITY_OFFLINE,           /**< 输入所属子板已确认离线。 */
} io_sample_quality_t;

/** @brief 一次数字输入采样的完整快照。 */
typedef struct {
    bool                level;        /**< 原始电平；仅在 quality=VALID 时可用于新判断。 */
    io_sample_quality_t quality;      /**< 样本质量。 */
    uint64_t            timestamp_ms; /**< 最近一次有效输入快照时间。 */
    uint32_t            sequence;     /**< 所属子板输入快照递增序号。 */
} io_di_sample_t;

/** @brief 一次 ADC 采样的完整快照。 */
typedef struct {
    int                 raw;          /**< 原始码值；仅 quality=VALID 时可用于新判断。 */
    int                 millivolt;    /**< 电压（mV）；未换算或无效时为 0。 */
    int                 milliamp;     /**< 电流（mA）；未换算或无效时为 0。 */
    io_sample_quality_t quality;      /**< 样本质量。 */
    uint64_t            timestamp_ms; /**< 最近一次有效采样时间。 */
} io_adc_sample_t;

/** @brief 一次脉冲计数采样的完整快照。 */
typedef struct {
    int                 raw;          /**< 计数值；仅 quality=VALID 时可用于新判断。 */
    io_sample_quality_t quality;      /**< 样本质量。 */
    uint64_t            timestamp_ms; /**< 最近一次有效采样时间。 */
} io_pulse_sample_t;

#ifdef __cplusplus
}
#endif

#endif /* COMMON_IO_SAMPLE_H */
