#ifndef DOMAIN_MODEL_CHEMICAL_CHANNEL_H
#define DOMAIN_MODEL_CHEMICAL_CHANNEL_H

#include "domain/model/domain_enums.h"
#include <stdbool.h>

/**
 * @file chemical_channel.h
 * @brief 定义化学剂通道模型。
 */
/**
 * @brief 描述一条化学剂供给通道。
 */
typedef struct chemical_channel_t
{
    /**< 通道 ID。 */
    char channel_id[32];
    /**< 通道名称。 */
    char channel_name[32];
    /**< 当前是否启用。 */
    bool enabled;
    /**< 资源状态。 */
    resource_state_t resource_state;
    /**< 低液位阈值。 */
    int low_level_threshold;
    /**< 默认投加时长，单位毫秒。 */
    int default_dose_ms;
    /**< 资源故障策略。 */
    fault_policy_t fault_policy;
} chemical_channel_t;

/**
 * @brief 初始化化学剂通道对象。
 * @param chemical_channel 通道对象，不能为空。
 * @param channel_id 通道 ID，可为空。
 * @param channel_name 通道名称，可为空。
 */
void chemical_channel_init(chemical_channel_t *chemical_channel, const char *channel_id, const char *channel_name);

#endif
