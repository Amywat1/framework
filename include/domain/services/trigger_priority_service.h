#ifndef DOMAIN_SERVICES_TRIGGER_PRIORITY_SERVICE_H
#define DOMAIN_SERVICES_TRIGGER_PRIORITY_SERVICE_H

#include "domain/model/wash_trigger_event.h"

/**
 * @file trigger_priority_service.h
 * @brief 定义竞争事件优先级规则。
 */

/**
 * @brief 比较两个触发事件的优先级高低。
 *
 * @param left_event 左侧事件，可为空。
 * @param right_event 右侧事件，可为空。
 * @return 左侧优先级更高时返回正数，相等返回 0，更低返回负数。
 */
int trigger_priority_service_compare(const wash_trigger_event_t *left_event, const wash_trigger_event_t *right_event);

#endif
