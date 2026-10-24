/**
 * @file    job_types.h
 * @brief   作业与订单共享类型
 */

#ifndef DOMAIN_JOB_JOB_TYPES_H
#define DOMAIN_JOB_JOB_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/op_mode/op_mode_types.h"

#include <stdint.h>

/**
 * @brief  容量依据（按已接入与规划机型）
 *
 * | 项目         | 实测或规划值 | 容量 | 说明 |
 * |--------------|--------------|------|------|
 * | 同时在洗作业 | 单车机型 1；连续多车规划不超过 8 | 8 | 静态槽覆盖连续多车 |
 * | 内存订单簿   | 云端短时堆积 | 16 | 掉电丢弃，只抗入队抖动 |
 */
#define JOB_ACTIVE_CAP       8U
#define JOB_ORDER_QUEUE_CAP 16U

/**
 * @brief  一笔待放行或已放行的作业描述
 */
typedef struct {
    uint64_t    order_id;       /**< 订单号；0 表示入队时由框架分配 */
    uint64_t    job_id;         /**< 放行后的作业号；未放行时为 0 */
    uint64_t    correlation_id; /**< 云端关联号，可与 order_id 相同 */
    uint64_t    enqueued_ms;    /**< 入队单调时钟，供有效期淘汰 */
    wash_mode_t mode;           /**< 套餐选择子，框架不解释 */
} job_desc_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_JOB_JOB_TYPES_H */
