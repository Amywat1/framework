#ifndef DOMAIN_MODEL_WASH_TRIGGER_EVENT_H
#define DOMAIN_MODEL_WASH_TRIGGER_EVENT_H

#include "domain/model/domain_enums.h"

/**
 * @file wash_trigger_event.h
 * @brief 定义驱动会话推进的触发事件。
 */
/**
 * @brief 描述一条驱动运行时推进的触发事件。
 */
typedef struct wash_trigger_event_t
{
    /**< 触发事件 ID。 */
    char trigger_id[32];
    /**< 触发类型。 */
    trigger_type_t trigger_type;
    /**< 关联会话 ID。 */
    char session_id[32];
    /**< 关联执行 ID。 */
    char execution_id[32];
    /**< 触发来源。 */
    char source[32];
    /**< 发生时间。 */
    unsigned long occurred_at_ms;
    /**< 关联键。 */
    char correlation_key[64];
    /**< 负载引用。 */
    char payload_ref[64];
    /**< 信号码。 */
    char signal_code[64];
    /**< 程序 ID。 */
    char program_id[32];
} wash_trigger_event_t;

/**
 * @brief 初始化一条触发事件。
 * @param wash_trigger_event 触发事件对象，不能为空。
 * @param trigger_type 触发类型。
 * @param program_id 程序 ID，可为空。
 * @param signal_code 信号码，可为空。
 * @param correlation_key 关联键，可为空。
 * @param occurred_at_ms 发生时间。
 */
void wash_trigger_event_init(wash_trigger_event_t *wash_trigger_event, trigger_type_t trigger_type,
                             const char *program_id, const char *signal_code, const char *correlation_key,
                             unsigned long occurred_at_ms);

#endif
