/**
 * @file    wash_snapshot_internal.h
 * @brief   洗车读模型内部更新接口（仅 projection 使用）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_TELEMETRY_WASH_SNAPSHOT_INTERNAL_H
#define DOMAIN_TELEMETRY_WASH_SNAPSHOT_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/wash/model/wash_types.h"

/**
 * @brief  会话启动时更新洗车模式
 */
void wash_snapshot_on_session_started(wash_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_WASH_SNAPSHOT_INTERNAL_H */
