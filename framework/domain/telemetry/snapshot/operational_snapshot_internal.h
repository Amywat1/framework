/**
 * @file    operational_snapshot_internal.h
 * @brief   运行模式读模型内部更新接口（仅 projection 使用）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_TELEMETRY_OPERATIONAL_SNAPSHOT_INTERNAL_H
#define DOMAIN_TELEMETRY_OPERATIONAL_SNAPSHOT_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/telemetry/snapshot/operational_snapshot.h"

/**
 * @brief  更新运行模式快照
 */
void operational_snapshot_update(const operational_snapshot_t *snap);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_OPERATIONAL_SNAPSHOT_INTERNAL_H */
