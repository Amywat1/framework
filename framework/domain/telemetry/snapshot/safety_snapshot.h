/**
 * @file    safety_snapshot.h
 * @brief   安全/报警读模型（线程安全快照）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_TELEMETRY_SAFETY_SNAPSHOT_H
#define DOMAIN_TELEMETRY_SAFETY_SNAPSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/safety/model/alarm_types.h"
#include "framework/domain/safety/model/safety_types.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    safety_posture_t   posture;
    bool               blocking_active;
    uint32_t           top_alarm_code;
    unsigned           active_alarm_count;
    alarm_instance_t   active_list[ALARM_ACTIVE_MAX];
} safety_snapshot_t;

/**
 * @brief  获取安全快照副本
 */
safety_snapshot_t safety_snapshot_get(void);

/**
 * @brief  是否存在告警态（blocking 或 LOCKOUT，读缓存）
 */
bool safety_snapshot_is_warning_active(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_SAFETY_SNAPSHOT_H */
