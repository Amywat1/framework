#ifndef DOMAIN_MODEL_SEGMENT_EXCEPTION_POLICY_H
#define DOMAIN_MODEL_SEGMENT_EXCEPTION_POLICY_H

#include "domain/model/domain_enums.h"

/**
 * @file segment_exception_policy.h
 * @brief 定义工步段异常策略。
 */
/**
 * @brief 描述工步段各类异常的处理策略集合。
 */
typedef struct segment_exception_policy_t
{
    /**< 位置丢失时的处理策略。 */
    exception_strategy_t on_position_lost;
    /**< 跟随丢失时的处理策略。 */
    exception_strategy_t on_follow_lost;
    /**< 工步超时时的处理策略。 */
    exception_strategy_t on_segment_timeout;
    /**< 退出超时时的处理策略。 */
    exception_strategy_t on_exit_timeout;
    /**< 退出失败时的处理策略。 */
    exception_strategy_t on_exit_failure;
} segment_exception_policy_t;

/**
 * @brief 将工步段异常策略初始化为默认值。
 * @param segment_exception_policy 异常策略对象，不能为空。
 */
void segment_exception_policy_init(segment_exception_policy_t *segment_exception_policy);

#endif
