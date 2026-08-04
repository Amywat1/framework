/**
 * @file    cloud_point_watcher.h
 * @brief   云端物模型 ON_CHANGE 点位变更检测
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    周期 poll 比对 shadow 值，变更时发布 EVT_CLOUD_POINT_DIRTY（param=点位表索引）。
 */

#ifndef CLOUD_CLOUD_POINT_WATCHER_H
#define CLOUD_CLOUD_POINT_WATCHER_H

#include "cloud/cloud_point.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化变更检测（对每个 ON_CHANGE 可读点位建立 shadow）
 */
sw_err_t cloud_point_watcher_init(const cloud_point_entry_t *entries, size_t count);

/**
 * @brief  周期检测变更并发布 EVT_CLOUD_POINT_DIRTY
 */
void cloud_point_watcher_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* CLOUD_CLOUD_POINT_WATCHER_H */
