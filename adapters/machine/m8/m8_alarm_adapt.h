/**
 * @file    m8_alarm_adapt.h
 * @brief   M8 报警适配初始化接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef ADAPTERS_MACHINE_M8_ALARM_ADAPT_H
#define ADAPTERS_MACHINE_M8_ALARM_ADAPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdbool.h>

/**
 * @brief  初始化 M8 报警适配（注册 IO 轮询和急停复位回调到 alarm_core）
 * @note   须在 alarm_core_init() 之后调用
 */
sw_err_t m8_alarm_adapt_init(void);

/**
 * @brief  VFD Modbus 通信丢失（由 m8_vfd_setup 事件回调触发）
 */
void m8_alarm_on_vfd_comm_lost(bool is_brush);

/**
 * @brief  VFD Modbus 通信恢复
 */
void m8_alarm_on_vfd_comm_restored(bool is_brush);

/**
 * @brief  VFD 故障状态变化（由 drv_vfd monitor worker 检测后经 event_cb 触发）
 * @param  is_brush   true=刷子 VFD，false=龙门 VFD
 * @param  has_fault  true=故障激活，false=故障消除
 */
void m8_alarm_on_vfd_fault(bool is_brush, bool has_fault);

/**
 * @brief  VFD 电流缓存已更新（由 drv_vfd monitor worker 经 event_cb 触发）
 * @param  is_brush  true=刷子 VFD
 * @note   函数内部读取缓存值并应用 M8 专属的电流异常阈值判断逻辑
 */
void m8_alarm_on_vfd_current_update(bool is_brush);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_ALARM_ADAPT_H */
