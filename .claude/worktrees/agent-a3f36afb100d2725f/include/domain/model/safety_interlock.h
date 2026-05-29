#ifndef DOMAIN_MODEL_SAFETY_INTERLOCK_H
#define DOMAIN_MODEL_SAFETY_INTERLOCK_H

#include "domain/model/domain_enums.h"

/**
 * @file safety_interlock.h
 * @brief 定义安全联锁模型。
 */
/**
 * @brief 描述一条安全联锁配置。
 */
typedef struct safety_interlock_t
{
    /**< 联锁 ID。 */
    char interlock_id[32];
    /**< 联锁名称。 */
    char interlock_name[32];
    /**< 当前联锁状态。 */
    interlock_state_t current_state;
    /**< 触发后的动作。 */
    trip_action_t trip_action;
    /**< 复位规则。 */
    reset_rule_t reset_rule;
} safety_interlock_t;

/**
 * @brief 初始化安全联锁对象。
 * @param safety_interlock 联锁对象，不能为空。
 * @param interlock_id 联锁 ID，可为空。
 * @param interlock_name 联锁名称，可为空。
 */
void safety_interlock_init(safety_interlock_t *safety_interlock, const char *interlock_id, const char *interlock_name);

#endif
