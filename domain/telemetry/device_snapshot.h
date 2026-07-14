/**
 * @file    device_snapshot.h
 * @brief   统一设备遥测快照（单锁原子读模型）
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#ifndef DOMAIN_TELEMETRY_DEVICE_SNAPSHOT_H
#define DOMAIN_TELEMETRY_DEVICE_SNAPSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/telemetry/snapshot/operational_snapshot.h"
#include "domain/telemetry/snapshot/safety_snapshot.h"
#include "domain/telemetry/snapshot/wash_snapshot.h"

/**
 * @brief  完整设备遥测快照，三子域以单锁原子读取
 */
typedef struct {
    operational_snapshot_t op;
    safety_snapshot_t      safety;
    wash_snapshot_t        wash;
} device_snapshot_t;

/**
 * @brief  原子获取完整遥测快照（内部加一次锁）
 */
device_snapshot_t device_snapshot_get(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_DEVICE_SNAPSHOT_H */
