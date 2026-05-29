#ifndef DOMAIN_MODEL_EXIT_ACTION_H
#define DOMAIN_MODEL_EXIT_ACTION_H

#include <stdbool.h>

/**
 * @file exit_action.h
 * @brief 定义固定退出动作集合。
 */
/**
 * @brief 描述工步退出阶段需要执行的固定动作集合。
 */
typedef struct segment_exit_actions_t
{
    /**< 是否停止顶刷。 */
    bool stop_roof_brush;
    /**< 是否停止侧刷。 */
    bool stop_side_brush;
    /**< 是否停止化学剂。 */
    bool stop_chemical;
    /**< 是否关闭纯水。 */
    bool close_ro_water;
    /**< 是否关闭风机。 */
    bool close_dryer;
    /**< 是否回零顶刷。 */
    bool roof_brush_home;
} segment_exit_actions_t;

/**
 * @brief 将退出动作集合初始化为空动作。
 * @param segment_exit_actions 退出动作集合，不能为空。
 */
void segment_exit_actions_init(segment_exit_actions_t *segment_exit_actions);
/**
 * @brief 判断退出动作集合中是否包含任一动作。
 * @param segment_exit_actions 退出动作集合。
 * @return 存在至少一个动作返回 `true`，否则返回 `false`。
 */
bool segment_exit_actions_has_any(const segment_exit_actions_t *segment_exit_actions);

#endif
