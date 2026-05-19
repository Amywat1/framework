#ifndef DOMAIN_MODEL_CHEMICAL_ACTION_H
#define DOMAIN_MODEL_CHEMICAL_ACTION_H

#include "domain/model/domain_enums.h"

/**
 * @file chemical_action.h
 * @brief 定义化学剂动作模型。
 */
/**
 * @brief 描述一条化学剂喷洒动作。
 */
typedef struct chemical_action_t
{
    /**< 目标通道 ID。 */
    char channel_id[32];
    /**< 启动条件。 */
    chemical_start_condition_t start_condition;
    /**< 动作持续时间，单位毫秒。 */
    int duration_ms;
    /**< 最大重试次数。 */
    int retry_limit;
} chemical_action_t;

/**
 * @brief 初始化化学剂动作对象。
 * @param chemical_action 动作对象，不能为空。
 * @param channel_id 目标通道 ID，可为空。
 * @param duration_ms 默认持续时间，单位毫秒。
 */
void chemical_action_init(chemical_action_t *chemical_action, const char *channel_id, int duration_ms);

#endif
