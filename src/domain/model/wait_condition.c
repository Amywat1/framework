#include "domain/model/wait_condition.h"

#include <stdio.h>
#include <string.h>

void wait_condition_reset(wait_condition_t *wait_condition)
{
    if (wait_condition == 0)
    {
        return;
    }
    memset(wait_condition, 0, sizeof(*wait_condition));
}

void wait_condition_arm(wait_condition_t *wait_condition, const char *execution_id, const char *reason_code,
                        unsigned long armed_at_ms, unsigned long timeout_at_ms, wait_timeout_policy_t timeout_policy,
                        unsigned long wait_condition_sequence)
{
    if (wait_condition == 0)
    {
        return;
    }

    wait_condition_reset(wait_condition);
    snprintf(wait_condition->wait_condition_id, sizeof(wait_condition->wait_condition_id), "wait-%08lx-%08lx",
             armed_at_ms & 0xfffffffful, wait_condition_sequence & 0xfffffffful);
    if (execution_id != 0)
    {
        strncpy(wait_condition->execution_id, execution_id, sizeof(wait_condition->execution_id) - 1);
    }
    if (reason_code != 0)
    {
        strncpy(wait_condition->reason_code, reason_code, sizeof(wait_condition->reason_code) - 1);
    }
    wait_condition->armed_at_ms = armed_at_ms;
    wait_condition->timeout_at_ms = timeout_at_ms;
    wait_condition->timeout_policy = timeout_policy;
    wait_condition->active = true;
}

bool wait_condition_is_timed_out(const wait_condition_t *wait_condition, unsigned long current_time_ms)
{
    if (wait_condition == 0 || wait_condition->active == false)
    {
        return false;
    }
    return current_time_ms >= wait_condition->timeout_at_ms;
}

void wait_condition_mark_done(wait_condition_t *wait_condition)
{
    if (wait_condition == 0)
    {
        return;
    }
    wait_condition->active = false;
}
