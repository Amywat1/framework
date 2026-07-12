/**
 * @file    safety_snapshot_internal.h
 * @brief   安全读模型内部更新接口（仅 projection 使用）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_TELEMETRY_SAFETY_SNAPSHOT_INTERNAL_H
#define DOMAIN_TELEMETRY_SAFETY_SNAPSHOT_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/telemetry/snapshot/safety_snapshot.h"

/**
 * @brief  更新安全快照
 */
void safety_snapshot_update(const safety_snapshot_t *snap);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_SAFETY_SNAPSHOT_INTERNAL_H */
