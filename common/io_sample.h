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

#ifdef __cplusplus
}
#endif

#endif /* COMMON_IO_SAMPLE_H */
