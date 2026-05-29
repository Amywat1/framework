#ifndef DOMAIN_MODEL_WASH_SEGMENT_H
#define DOMAIN_MODEL_WASH_SEGMENT_H

#include <stdbool.h>

#include "domain/model/conditional_control.h"
#include "domain/model/domain_enums.h"
#include "domain/model/exit_action.h"
#include "domain/model/position_trigger.h"
#include "domain/model/segment_exception_policy.h"
#include "domain/model/segment_motion_plan.h"

/**
 * @file wash_segment.h
 * @brief 定义强类型工步段模型。
 */
/**
 * @brief 描述工步段在运行期持续保持的控制输出。
 */
typedef struct segment_continuous_controls_t
{
    /**< 是否启用顶刷跟随。 */
    bool roof_brush_follow;
    /**< 是否启用侧刷。 */
    bool side_brush_enabled;
    /**< 是否启用纯水。 */
    bool ro_water_enabled;
    /**< 是否启用风机。 */
    bool dryer_enabled;
} segment_continuous_controls_t;

/**
 * @brief 描述工步段的完成判定条件。
 */
typedef struct segment_completion_condition_t
{
    /**< 完成触发条件。 */
    position_trigger_t trigger;
    /**< 是否要求全部条件控制已启动。 */
    bool requires_all_controls_started;
} segment_completion_condition_t;

/**
 * @brief 描述一段完整洗车工步。
 */
typedef struct wash_segment_t
{
    /**< 工步段 ID。 */
    char segment_id[32];
    /**< 工步段名称。 */
    char segment_name[32];
    /**< 顺序号。 */
    int sequence_no;
    /**< 工步段类型。 */
    segment_kind_t segment_kind;
    /**< 移动计划。 */
    segment_motion_plan_t motion_plan;
    /**< 持续控制配置。 */
    segment_continuous_controls_t continuous_controls;
    /**< 条件控制集合。 */
    conditional_control_t conditional_controls[MAX_SEGMENT_CONDITIONAL_CONTROLS];
    /**< 条件控制数量。 */
    int conditional_control_count;
    /**< 完成判定条件。 */
    segment_completion_condition_t completion_condition;
    /**< 退出动作集合。 */
    segment_exit_actions_t exit_actions;
    /**< 工步超时，单位毫秒。 */
    int segment_timeout_ms;
    /**< 退出超时，单位毫秒。 */
    int exit_timeout_ms;
    /**< 异常策略集合。 */
    segment_exception_policy_t exception_policy;
} wash_segment_t;

/**
 * @brief 初始化工步段对象。
 * @param wash_segment 工步段对象，不能为空。
 * @param segment_id 工步段 ID，可为空。
 * @param segment_name 工步段名称，可为空。
 * @param sequence_no 顺序号。
 */
void wash_segment_init(wash_segment_t *wash_segment, const char *segment_id, const char *segment_name, int sequence_no);
/**
 * @brief 判断工步段配置是否合法。
 * @param wash_segment 工步段对象。
 * @return 合法返回 `true`，否则返回 `false`。
 */
bool wash_segment_is_valid(const wash_segment_t *wash_segment);

#endif
