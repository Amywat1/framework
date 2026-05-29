#ifndef DOMAIN_MODEL_WAIT_CONDITION_H
#define DOMAIN_MODEL_WAIT_CONDITION_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file wait_condition.h
 * @brief 定义段内与收尾超时条件。
 */
/**
 * @brief 描述一条已装配的等待超时条件。
 */
typedef struct wait_condition_t
{
    /**< 等待条件 ID。 */
    char wait_condition_id[32];
    /**< 关联执行 ID。 */
    char execution_id[32];
    /**< 超时原因码。 */
    char reason_code[64];
    /**< 装配时间。 */
    unsigned long armed_at_ms;
    /**< 超时时间点。 */
    unsigned long timeout_at_ms;
    /**< 超时策略。 */
    wait_timeout_policy_t timeout_policy;
    /**< 当前是否处于激活状态。 */
    bool active;
} wait_condition_t;

/**
 * @brief 重置等待条件对象。
 *
 * @param wait_condition 等待对象，不能为空。
 *
 * @note 等待对象只描述超时事实，不负责直接改写执行或会话终态。
 */
void wait_condition_reset(wait_condition_t *wait_condition);
/**
 * @brief 装配一条新的等待超时条件。
 * @param wait_condition 等待对象，不能为空。
 * @param execution_id 关联执行 ID，可为空。
 * @param reason_code 超时原因码，可为空。
 * @param armed_at_ms 装配时间。
 * @param timeout_at_ms 超时时间点。
 * @param timeout_policy 超时策略。
 * @param wait_condition_sequence 等待条件序列号。
 */
void wait_condition_arm(wait_condition_t *wait_condition, const char *execution_id, const char *reason_code,
                        unsigned long armed_at_ms, unsigned long timeout_at_ms, wait_timeout_policy_t timeout_policy,
                        unsigned long wait_condition_sequence);
/**
 * @brief 判断等待条件是否已经超时。
 * @param wait_condition 等待对象。
 * @param current_time_ms 当前时间。
 * @return 已超时返回 `true`，否则返回 `false`。
 */
bool wait_condition_is_timed_out(const wait_condition_t *wait_condition, unsigned long current_time_ms);
/**
 * @brief 将等待条件标记为完成。
 * @param wait_condition 等待对象，不能为空。
 */
void wait_condition_mark_done(wait_condition_t *wait_condition);

#endif
