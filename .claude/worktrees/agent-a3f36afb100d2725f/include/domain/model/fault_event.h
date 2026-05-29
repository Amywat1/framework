#ifndef DOMAIN_MODEL_FAULT_EVENT_H
#define DOMAIN_MODEL_FAULT_EVENT_H

#include "domain/model/domain_enums.h"
#include <stdbool.h>

/**
 * @file fault_event.h
 * @brief 定义故障事件模型。
 */
/**
 * @brief 描述一条故障事件记录。
 */
typedef struct fault_event_t
{
    /**< 事件 ID。 */
    char event_id[32];
    /**< 事件消息文本。 */
    char message[128];
    /**< 故障严重级别。 */
    fault_severity_t severity;
    /**< 故障类别。 */
    fault_class_t fault_class;
    /**< 处置方式。 */
    int disposition;
    /**< 是否需要人工确认。 */
    bool operator_ack_required;
} fault_event_t;

/**
 * @brief 初始化故障事件对象。
 * @param fault_event 故障事件对象，不能为空。
 * @param event_id 事件 ID，可为空。
 * @param message 事件消息，可为空。
 * @param fault_class 故障类别。
 * @param severity 故障严重级别。
 */
void fault_event_init(fault_event_t *fault_event, const char *event_id, const char *message, fault_class_t fault_class,
                      fault_severity_t severity);

#endif
