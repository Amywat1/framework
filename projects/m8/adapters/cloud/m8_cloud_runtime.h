/**
 * @file    m8_cloud_runtime.h
 * @brief   M8 云端物模型运行时状态与工具
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_RUNTIME_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_RUNTIME_H

#include "framework/common/point_table/point_table.h"
#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 三态枚举命令：停止 */
#define M8_CLOUD_TRI_STOP   0
/** @brief 三态枚举命令：负向（后退/下降/反转/松开） */
#define M8_CLOUD_TRI_NEG   (-1)
/** @brief 三态枚举命令：正向（前进/上升/正转/锁紧） */
#define M8_CLOUD_TRI_POS    1

/**
 * @brief  读取 DI 原始电平（已按 active_low 转为有效触发态）
 */
bool m8_cloud_di_active(io_di_t pin, bool active_low);

/**
 * @brief  读取 DI 并取反（防撞胶条类历史遗留显示逻辑）
 */
bool m8_cloud_di_inverted(io_di_t pin, bool active_low);

/**
 * @brief  标记用户中止洗车中（sts_custom_stopping）
 */
void m8_cloud_runtime_set_custom_stopping(bool active);

/**
 * @brief  记录洗车统计（供遥测读取）
 */
void m8_cloud_runtime_on_wash_started(void);
void m8_cloud_runtime_on_wash_completed(void);
void m8_cloud_runtime_on_wash_failed(void);

/**
 * @brief  布尔配置项读写（功能按钮/监测开关）
 */
bool m8_cloud_runtime_get_bool(const char *id);
void m8_cloud_runtime_set_bool(const char *id, bool value);

/**
 * @brief  持续型水路/风机/照明命令态读写
 */
bool m8_cloud_runtime_get_hold(const char *id);
void m8_cloud_runtime_set_hold(const char *id, bool active);

/**
 * @brief  三态枚举命令当前值读写
 */
int32_t m8_cloud_runtime_get_tri(const char *id);
void m8_cloud_runtime_set_tri(const char *id, int32_t value);

/**
 * @brief  刷新子板 IO 快照（仅在停止中时更新）
 */
void m8_cloud_runtime_refresh_io_snapshot(void);

/**
 * @brief  读取子板 IO 字符串遥测
 */
sw_err_t m8_cloud_runtime_get_io_str(const char *id, point_value_t *out);

bool m8_cloud_runtime_is_custom_stopping(void);
int32_t m8_cloud_runtime_get_port_number(void);
int32_t m8_cloud_runtime_get_wash_today(void);
int32_t m8_cloud_runtime_get_wash_start(void);
int32_t m8_cloud_runtime_get_wash_complete(void);
int32_t m8_cloud_runtime_get_wash_failed(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_RUNTIME_H */
