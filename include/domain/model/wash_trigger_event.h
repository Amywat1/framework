#ifndef DOMAIN_MODEL_WASH_TRIGGER_EVENT_H
#define DOMAIN_MODEL_WASH_TRIGGER_EVENT_H

#include "domain/model/domain_enums.h"

/**
 * @file wash_trigger_event.h
 * @brief 定义驱动会话推进的触发事件。
 */
typedef struct wash_trigger_event_t
{
    char trigger_id[32];
    trigger_type_t trigger_type;
    char session_id[32];
    char execution_id[32];
    char source[32];
    unsigned long occurred_at_ms;
    char correlation_key[64];
    char payload_ref[64];
    char signal_code[64];
    char program_id[32];
} wash_trigger_event_t;

void wash_trigger_event_init(wash_trigger_event_t *wash_trigger_event, trigger_type_t trigger_type,
                             const char *program_id, const char *signal_code, const char *correlation_key,
                             unsigned long occurred_at_ms);

#endif
