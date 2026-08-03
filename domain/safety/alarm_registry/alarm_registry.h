/**
 * @file    alarm_registry.h
 * @brief   报警注册表聚合根
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef DOMAIN_SAFETY_ALARM_REGISTRY_H
#define DOMAIN_SAFETY_ALARM_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/safety/model/alarm_types.h"

#include <stdbool.h>
#include <stdint.h>

sw_err_t alarm_registry_init(void);
sw_err_t alarm_registry_load_catalog(const alarm_def_t *defs, unsigned count);

sw_err_t alarm_registry_trigger(uint32_t code);
sw_err_t alarm_registry_clear(uint32_t code);
sw_err_t alarm_registry_reevaluate_group(motion_reeval_group_id_t group);

void alarm_registry_on_wash_session_started(void);
void alarm_registry_on_wash_session_ended(void);
/**
 * @brief 请求复位全部可人工复位告警。
 * @note  仅清除故障条件已经消失的告警；条件仍成立时保留活动状态。
 */
void alarm_registry_reset_all(void);

bool             alarm_registry_is_active(uint32_t code);
bool             alarm_registry_has_blocking_active(void);
unsigned         alarm_registry_get_session_journal(uint32_t *buf, unsigned max);
unsigned         alarm_registry_copy_active_projection(alarm_instance_t *list,
                                                       unsigned          list_max,
                                                       bool             *blocking_out,
                                                       uint32_t         *top_out);
safety_posture_t alarm_registry_safety_posture(void);

/**
 * @brief  一次持锁读出完整安全投影（活动表 + 阻塞标志 + 最高码 + 安全姿态）
 * @param  list         活动告警输出缓冲；可为 NULL（只要计数与聚合值）
 * @param  list_max     list 容量；为 0 时视同 list 为 NULL
 * @param  blocking_out 输出是否存在 MAJOR 及以上告警；可为 NULL
 * @param  top_out      输出当前最高级别告警码；可为 NULL
 * @param  posture_out  输出安全姿态；可为 NULL
 * @return 实际写入 list 的条目数（list 为 NULL 时返回当前活动告警总数）
 * @note   相对于分别调用 copy_active_projection + safety_posture，
 *         本接口只取一次锁，保证四项数据来自同一时刻的一致视图。
 */
unsigned alarm_registry_copy_safety_view(alarm_instance_t *list,
                                         unsigned          list_max,
                                         bool             *blocking_out,
                                         uint32_t         *top_out,
                                         safety_posture_t *posture_out);

unsigned alarm_registry_pull_events(alarm_domain_event_t *buf, unsigned max);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_ALARM_REGISTRY_H */
