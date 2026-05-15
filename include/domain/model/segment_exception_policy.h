#ifndef DOMAIN_MODEL_SEGMENT_EXCEPTION_POLICY_H
#define DOMAIN_MODEL_SEGMENT_EXCEPTION_POLICY_H

#include "domain/model/domain_enums.h"

/**
 * @file segment_exception_policy.h
 * @brief 定义工步段异常策略。
 */
typedef struct segment_exception_policy_t
{
    exception_strategy_t on_position_lost;
    exception_strategy_t on_follow_lost;
    exception_strategy_t on_segment_timeout;
    exception_strategy_t on_exit_timeout;
    exception_strategy_t on_exit_failure;
} segment_exception_policy_t;

void segment_exception_policy_init(segment_exception_policy_t *segment_exception_policy);

#endif
