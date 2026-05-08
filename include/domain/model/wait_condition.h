#ifndef DOMAIN_MODEL_WAIT_CONDITION_H
#define DOMAIN_MODEL_WAIT_CONDITION_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file wait_condition.h
 * @brief 定义段内与收尾超时条件。
 */
typedef struct wait_condition_t {
    char wait_condition_id[32];
    char execution_id[32];
    char reason_code[64];
    unsigned long armed_at_ms;
    unsigned long timeout_at_ms;
    wait_timeout_policy_t timeout_policy;
    bool active;
} wait_condition_t;

void wait_condition_reset(wait_condition_t *wait_condition);
void wait_condition_arm(wait_condition_t *wait_condition,
    const char *execution_id,
    const char *reason_code,
    unsigned long armed_at_ms,
    unsigned long timeout_at_ms,
    wait_timeout_policy_t timeout_policy,
    unsigned long wait_condition_sequence);
bool wait_condition_is_timed_out(const wait_condition_t *wait_condition, unsigned long current_time_ms);
void wait_condition_mark_done(wait_condition_t *wait_condition);

#endif
