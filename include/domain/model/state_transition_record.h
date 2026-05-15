#ifndef DOMAIN_MODEL_STATE_TRANSITION_RECORD_H
#define DOMAIN_MODEL_STATE_TRANSITION_RECORD_H

#include "domain/model/domain_enums.h"

/**
 * @file state_transition_record.h
 * @brief 定义状态迁移与拒绝/忽略记录。
 */
typedef struct state_transition_record_t
{
    char record_id[32];
    transition_entity_type_t entity_type;
    char entity_id[32];
    trigger_type_t trigger_type;
    char previous_state[32];
    char current_state[32];
    char result_code[32];
    char reason_code[64];
    unsigned long recorded_at_ms;
} state_transition_record_t;

void state_transition_record_init(state_transition_record_t *state_transition_record,
                                  transition_entity_type_t entity_type, const char *entity_id,
                                  trigger_type_t trigger_type, const char *previous_state, const char *current_state,
                                  const char *result_code, const char *reason_code, unsigned long recorded_at_ms);

#endif
