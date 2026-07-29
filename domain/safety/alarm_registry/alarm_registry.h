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

unsigned alarm_registry_pull_events(alarm_domain_event_t *buf, unsigned max);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_ALARM_REGISTRY_H */
