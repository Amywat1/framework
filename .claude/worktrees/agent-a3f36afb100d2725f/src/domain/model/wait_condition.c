#include "domain/model/wait_condition.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief 重置等待条件对象。
 * @param wait_condition 等待对象。
 */
void wait_condition_reset(wait_condition_t *wait_condition)
{
    if (wait_condition == 0)
    {
        return;
    }
    memset(wait_condition, 0, sizeof(*wait_condition));
}

/**
 * @brief 装配一条新的等待超时条件。
 * @param wait_condition 等待对象。
 * @param execution_id 关联执行 ID，可为空。
 * @param reason_code 超时原因码，可为空。
 * @param armed_at_ms 装配时间。
 * @param timeout_at_ms 超时时间点。
 * @param timeout_policy 超时策略。
 * @param wait_condition_sequence 等待条件序列号。
 */
void wait_condition_arm(wait_condition_t *wait_condition, const char *execution_id, const char *reason_code,
                        unsigned long armed_at_ms, unsigned long timeout_at_ms, wait_timeout_policy_t timeout_policy,
                        unsigned long wait_condition_sequence)
{
    if (wait_condition == 0)
    {
        return;
    }

    /* 先清空旧事实，避免复用对象时残留上一次等待条件的字段。 */
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

/**
 * @brief 判断等待条件是否已经超时。
 * @param wait_condition 等待对象。
 * @param current_time_ms 当前时间。
 * @return 已超时返回 `true`，否则返回 `false`。
 */
bool wait_condition_is_timed_out(const wait_condition_t *wait_condition, unsigned long current_time_ms)
{
    if (wait_condition == 0 || wait_condition->active == false)
    {
        return false;
    }
    return current_time_ms >= wait_condition->timeout_at_ms;
}

/**
 * @brief 将等待条件标记为完成。
 * @param wait_condition 等待对象。
 */
void wait_condition_mark_done(wait_condition_t *wait_condition)
{
    if (wait_condition == 0)
    {
        return;
    }
    wait_condition->active = false;
}
