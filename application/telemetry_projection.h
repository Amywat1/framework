/**
 * @file    telemetry_projection.h
 * @brief   设备遥测投影初始化（合并三子域投影）
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#ifndef APPLICATION_TELEMETRY_PROJECTION_H
#define APPLICATION_TELEMETRY_PROJECTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化遥测投影：订阅所有相关事件并做首次同步
 * @note   须在 operational_mode_init() 与 alarm_registry_init() 之后调用
 */
sw_err_t telemetry_projection_init(void);

/**
 * @brief  从当前事实源重建可拉取的遥测子域
 * @note   运行模式、安全和云连接可重建；洗车模式目前只有启动事件作为事实来源，
 *         因而仍保持事件驱动，不在此伪造重建结果。
 */
void telemetry_projection_sync_all(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_TELEMETRY_PROJECTION_H */
