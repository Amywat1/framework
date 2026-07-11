/**
 * @file    operational_snapshot.h
 * @brief   运行模式读模型（线程安全快照）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_TELEMETRY_OPERATIONAL_SNAPSHOT_H
#define DOMAIN_TELEMETRY_OPERATIONAL_SNAPSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/device_control/model/device_state.h"
#include <stdbool.h>

typedef struct
{
    operational_mode_t mode;
    bool               service_enabled;
    bool               estop_active;
} operational_snapshot_t;

/**
 * @brief  获取运行模式快照副本
 */
operational_snapshot_t operational_snapshot_get(void);

/**
 * @brief  是否停机态（与 op_mode_is_stopping 语义一致，读缓存）
 */
bool operational_snapshot_is_stopping(void);

/**
 * @brief  是否待机（与 op_mode_is_standby 语义一致，读缓存）
 */
bool operational_snapshot_is_standby(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_OPERATIONAL_SNAPSHOT_H */
