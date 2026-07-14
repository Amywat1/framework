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

#include "domain/command_gateway/op_mode_types.h"

#include <stdbool.h>

typedef struct {
    operational_mode_t mode;
    bool               service_enabled;
    bool               estop_active;
} operational_snapshot_t;

/**
 * @brief  获取运行模式快照副本（内部加一次锁）
 */
operational_snapshot_t operational_snapshot_get(void);

/**
 * @brief  判断是否处于停机态
 * @param  s  已获取的快照值
 * @return true：service 未使能，或模式为 INIT / EXCEPTION / RECOVERING
 * @note   调用方须先通过 operational_snapshot_get() 取一次快照再传入，
 *         以避免多次 get() 之间状态不一致。
 */
static inline bool operational_snapshot_is_stopping(operational_snapshot_t s)
{
    return !s.service_enabled
           || (s.mode == OP_MODE_INIT)
           || (s.mode == OP_MODE_EXCEPTION)
           || (s.mode == OP_MODE_RECOVERING);
}

/**
 * @brief  判断是否处于待机态
 * @param  s  已获取的快照值
 * @return true：模式为 IDLE 且 service 已使能
 * @note   调用方须先通过 operational_snapshot_get() 取一次快照再传入，
 *         以避免多次 get() 之间状态不一致。
 */
static inline bool operational_snapshot_is_standby(operational_snapshot_t s)
{
    return (s.mode == OP_MODE_IDLE) && s.service_enabled;
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_OPERATIONAL_SNAPSHOT_H */
