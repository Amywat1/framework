#ifndef DOMAIN_SERVICES_TRIGGER_PRIORITY_SERVICE_H
#define DOMAIN_SERVICES_TRIGGER_PRIORITY_SERVICE_H

#include "domain/model/wash_trigger_event.h"

/**
 * @file trigger_priority_service.h
 * @brief 定义竞争事件优先级规则。
 */
int trigger_priority_service_compare(const wash_trigger_event_t *left_event, const wash_trigger_event_t *right_event);

#endif
