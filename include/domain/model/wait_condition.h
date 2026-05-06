#ifndef DOMAIN_MODEL_WAIT_CONDITION_H
#define DOMAIN_MODEL_WAIT_CONDITION_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file wait_condition.h
 * @brief 定义执行中的等待条件与超时边界。
 */
typedef struct wait_condition_t {
    char wait_condition_id[32];
    char execution_id[32];
    trigger_type_t expected_trigger_type;
    char expected_signal[64];
    unsigned long armed_at_ms;
    unsigned long timeout_at_ms;
    wait_timeout_policy_t timeout_policy;
    unsigned int max_retry_count;
    unsigned int current_retry_count;
    bool active;
    bool satisfied;
} wait_condition_t;

void wait_condition_reset(wait_condition_t *wait_condition);
void wait_condition_arm(wait_condition_t *wait_condition,
    const char *execution_id,
    trigger_type_t expected_trigger_type,
    const char *expected_signal,
    unsigned long armed_at_ms,
    unsigned long timeout_at_ms,
    wait_timeout_policy_t timeout_policy,
    unsigned int max_retry_count,
    unsigned long wait_condition_sequence);
bool wait_condition_matches(const wait_condition_t *wait_condition, trigger_type_t trigger_type, const char *signal_code);
bool wait_condition_is_timed_out(const wait_condition_t *wait_condition, unsigned long current_time_ms);
bool wait_condition_can_retry(const wait_condition_t *wait_condition);
void wait_condition_mark_satisfied(wait_condition_t *wait_condition);
void wait_condition_rearm(wait_condition_t *wait_condition, unsigned long current_time_ms, unsigned long timeout_delta_ms);

#endif
