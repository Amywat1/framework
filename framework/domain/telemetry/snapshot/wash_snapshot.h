/**
 * @file    wash_snapshot.h
 * @brief   洗车会话读模型（线程安全快照）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_TELEMETRY_WASH_SNAPSHOT_H
#define DOMAIN_TELEMETRY_WASH_SNAPSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/wash/model/wash_types.h"

typedef struct
{
    wash_mode_t mode;
} wash_snapshot_t;

/**
 * @brief  获取洗车快照副本
 */
wash_snapshot_t wash_snapshot_get(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_WASH_SNAPSHOT_H */
