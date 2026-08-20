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

#include "domain/op_mode/op_mode_types.h"
#include "domain/safety/model/alarm_types.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 运行模式子域快照
 * ------------------------------------------------------------------------- */
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
 * @note   调用方须先通过 operational_snapshot_get() 取一次快照再传入。
 */
static inline bool operational_snapshot_is_stopping(operational_snapshot_t s)
{
    return !s.service_enabled || (s.mode == OP_MODE_INIT) || (s.mode == OP_MODE_STOPPED)
           || (s.mode == OP_MODE_ABORT_HOMING) || (s.mode == OP_MODE_SELF_CHECK) || (s.mode == OP_MODE_RECOVERING);
}

/**
 * @brief  判断是否处于运营待机态
 * @note   调用方须先通过 operational_snapshot_get() 取一次快照再传入。
 */
static inline bool operational_snapshot_is_standby(operational_snapshot_t s)
{
    return s.mode == OP_MODE_IDLE;
}

/* -------------------------------------------------------------------------
 * 安全/报警子域快照
 * ------------------------------------------------------------------------- */
typedef struct {
    safety_posture_t posture;
    bool             blocking_active;
    bool             cutout_unconfirmed; /**< 动力切断失败且尚无独立反馈确认 */
    uint32_t         top_alarm_code;
    unsigned         active_alarm_count;
    alarm_instance_t active_list[ALARM_ACTIVE_MAX];
} safety_snapshot_t;

/**
 * @brief  获取安全快照副本
 */
safety_snapshot_t safety_snapshot_get(void);

/**
 * @brief  是否存在告警态（blocking，读缓存）
 */
bool safety_snapshot_is_warning_active(void);

/* -------------------------------------------------------------------------
 * 洗车会话子域快照
 * ------------------------------------------------------------------------- */
typedef struct {
    wash_mode_t mode;
} wash_snapshot_t;

/* -------------------------------------------------------------------------
 * 连接性子域快照
 *
 * 云连接状态本身不是领域事实，但读侧（CLI / 诊断 / 状态上报）需要与运行模式、
 * 安全状态在同一次读取中保持一致，因此并入本快照。取值由 application 层的
 * 遥测投影从云端口刷新写入，领域层不依赖任何云端口。
 * ------------------------------------------------------------------------- */
typedef struct {
    bool cloud_connected;
} connectivity_snapshot_t;

/**
 * @brief  获取洗车快照副本
 */
wash_snapshot_t wash_snapshot_get(void);

/* -------------------------------------------------------------------------
 * 完整设备遥测快照
 * ------------------------------------------------------------------------- */

/**
 * @brief  完整设备遥测快照，各子域以单锁原子读取
 */
typedef struct {
    operational_snapshot_t  op;
    safety_snapshot_t       safety;
    wash_snapshot_t         wash;
    connectivity_snapshot_t connectivity;
} device_snapshot_t;

/**
 * @brief  原子获取完整遥测快照（内部加一次锁）
 */
device_snapshot_t device_snapshot_get(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_DEVICE_SNAPSHOT_H */
