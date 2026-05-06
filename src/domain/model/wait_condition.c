#include "domain/model/wait_condition.h"

#include <stdio.h>
#include <string.h>

void wait_condition_reset(wait_condition_t *wait_condition)
{
    if (wait_condition == 0) {
        return;
    }
    memset(wait_condition, 0, sizeof(*wait_condition));
}

void wait_condition_arm(wait_condition_t *wait_condition,
    const char *execution_id,
    trigger_type_t expected_trigger_type,
    const char *expected_signal,
    unsigned long armed_at_ms,
    unsigned long timeout_at_ms,
    wait_timeout_policy_t timeout_policy,
    unsigned int max_retry_count,
    unsigned long wait_condition_sequence)
{
    if (wait_condition == 0) {
        return;
    }

    wait_condition_reset(wait_condition);
    snprintf(wait_condition->wait_condition_id,
        sizeof(wait_condition->wait_condition_id),
        "wait-%08lx-%08lx",
        armed_at_ms & 0xfffffffful,
        wait_condition_sequence & 0xfffffffful);
    if (execution_id != 0) {
        strncpy(wait_condition->execution_id, execution_id, sizeof(wait_condition->execution_id) - 1);
    }
    if (expected_signal != 0) {
        strncpy(wait_condition->expected_signal, expected_signal, sizeof(wait_condition->expected_signal) - 1);
    }
    wait_condition->expected_trigger_type = expected_trigger_type;
    wait_condition->armed_at_ms = armed_at_ms;
    wait_condition->timeout_at_ms = timeout_at_ms;
    wait_condition->timeout_policy = timeout_policy;
    wait_condition->max_retry_count = max_retry_count;
    wait_condition->active = true;
}

bool wait_condition_matches(const wait_condition_t *wait_condition, trigger_type_t trigger_type, const char *signal_code)
{
    if (wait_condition == 0 || wait_condition->active == false) {
        return false;
    }
    if (wait_condition->expected_trigger_type != trigger_type) {
        return false;
    }
    if (signal_code == 0) {
        return false;
    }
    return strcmp(wait_condition->expected_signal, signal_code) == 0;
}

bool wait_condition_is_timed_out(const wait_condition_t *wait_condition, unsigned long current_time_ms)
{
    if (wait_condition == 0 || wait_condition->active == false) {
        return false;
    }
    return current_time_ms >= wait_condition->timeout_at_ms;
}

bool wait_condition_can_retry(const wait_condition_t *wait_condition)
{
    if (wait_condition == 0) {
        return false;
    }
    return wait_condition->current_retry_count < wait_condition->max_retry_count;
}

void wait_condition_mark_satisfied(wait_condition_t *wait_condition)
{
    if (wait_condition == 0) {
        return;
    }
    wait_condition->satisfied = true;
    wait_condition->active = false;
}

void wait_condition_rearm(wait_condition_t *wait_condition, unsigned long current_time_ms, unsigned long timeout_delta_ms)
{
    if (wait_condition == 0) {
        return;
    }
    wait_condition->current_retry_count += 1;
    wait_condition->armed_at_ms = current_time_ms;
    wait_condition->timeout_at_ms = current_time_ms + timeout_delta_ms;
    wait_condition->active = true;
    wait_condition->satisfied = false;
}
