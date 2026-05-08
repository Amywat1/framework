#ifndef DOMAIN_MODEL_EXIT_ACTION_H
#define DOMAIN_MODEL_EXIT_ACTION_H

#include <stdbool.h>

/**
 * @file exit_action.h
 * @brief 定义固定退出动作集合。
 */
typedef struct segment_exit_actions_t {
    bool stop_roof_brush;
    bool stop_side_brush;
    bool stop_chemical;
    bool close_ro_water;
    bool close_dryer;
    bool roof_brush_home;
} segment_exit_actions_t;

void segment_exit_actions_init(segment_exit_actions_t *segment_exit_actions);
bool segment_exit_actions_has_any(const segment_exit_actions_t *segment_exit_actions);

#endif
