/**
 * @file    device_snapshot_internal.h
 * @brief   统一遥测快照内部写接口（仅 telemetry_projection 使用）
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#ifndef DOMAIN_TELEMETRY_DEVICE_SNAPSHOT_INTERNAL_H
#define DOMAIN_TELEMETRY_DEVICE_SNAPSHOT_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/telemetry/device_snapshot.h"

/**
 * @brief  更新运行模式子域快照
 */
void device_snapshot_update_op(const operational_snapshot_t *op);

/**
 * @brief  更新安全/报警子域快照
 */
void device_snapshot_update_safety(const safety_snapshot_t *safety);

/**
 * @brief  更新洗车模式（会话启动时调用）
 */
void device_snapshot_set_wash_mode(wash_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_DEVICE_SNAPSHOT_INTERNAL_H */
