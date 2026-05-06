#ifndef APPLICATION_USE_CASES_PROCESS_WASH_TRIGGER_H
#define APPLICATION_USE_CASES_PROCESS_WASH_TRIGGER_H

#include "application/coordinators/system_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file process_wash_trigger.h
 * @brief 定义统一触发处理用例。
 */
operation_result_t process_wash_trigger_execute(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event);

#endif
