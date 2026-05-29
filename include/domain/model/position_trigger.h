#ifndef DOMAIN_MODEL_POSITION_TRIGGER_H
#define DOMAIN_MODEL_POSITION_TRIGGER_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file position_trigger.h
 * @brief 定义统一位置快照与位置触发器。
 */
/**
 * @brief 描述一次当前位置采样得到的快照。
 */
typedef struct position_snapshot_t
{
    /**< 位置测量值当前是否有效。 */
    bool position_valid;
    /**< 龙门架绝对位置，单位毫米。 */
    int gantry_absolute_mm;
    /**< 到车头的距离，单位毫米。 */
    int distance_to_vehicle_head_mm;
    /**< 到车尾的距离，单位毫米。 */
    int distance_to_vehicle_tail_mm;
    /**< 是否已到达原点。 */
    bool home_reached;
    /**< 是否已到达车头参考点。 */
    bool head_reached;
    /**< 是否已到达车尾参考点。 */
    bool tail_reached;
} position_snapshot_t;

/**
 * @brief 描述一条位置触发判定规则。
 */
typedef struct position_trigger_t
{
    /**< 参与判定的位置参考量。 */
    position_reference_t reference;
    /**< 判定比较模式。 */
    position_compare_mode_t compare_mode;
    /**< 主比较值，单位毫米。 */
    int value_mm;
    /**< 区间比较的上界值，单位毫米。 */
    int upper_value_mm;
    /**< 判定容差，单位毫米。 */
    int tolerance_mm;
} position_trigger_t;

/**
 * @brief 将位置触发器初始化为零值状态。
 * @param position_trigger 待初始化触发器。
 */
void position_trigger_init(position_trigger_t *position_trigger);
/**
 * @brief 判断位置触发器配置是否合法。
 * @param position_trigger 待校验触发器。
 * @return 合法返回 `true`，否则返回 `false`。
 */
bool position_trigger_is_valid(const position_trigger_t *position_trigger);
/**
 * @brief 判断当前位置快照是否满足触发条件。
 * @param position_trigger 位置触发器。
 * @param position_snapshot 当前位置快照。
 * @return 命中触发条件返回 `true`，否则返回 `false`。
 */
bool position_trigger_matches(const position_trigger_t *position_trigger, const position_snapshot_t *position_snapshot);
/**
 * @brief 按触发器参考量提取当前测量值。
 * @param position_trigger 位置触发器。
 * @param position_snapshot 当前位置快照。
 * @return 对应参考量的测量值；参数非法时返回 `0`。
 */
int position_trigger_measure(const position_trigger_t *position_trigger, const position_snapshot_t *position_snapshot);

#endif
