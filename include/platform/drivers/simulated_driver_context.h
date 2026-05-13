#ifndef PLATFORM_DRIVERS_SIMULATED_DRIVER_CONTEXT_H
#define PLATFORM_DRIVERS_SIMULATED_DRIVER_CONTEXT_H

#include <stdbool.h>

#include "domain/ports/sensor_port.h"

/**
 * @file simulated_driver_context.h
 * @brief 定义仿真驱动共享上下文。
 */
typedef struct simulated_driver_context_t {
    runtime_snapshot_t runtime_snapshot;
    bool roof_brush_follow_enabled;
    bool side_brush_enabled;
    bool chemical_enabled;
    bool ro_water_enabled;
    bool dryer_enabled;
    bool roof_follow_feedback_available;
    bool roof_stop_feedback_available;
    bool side_stop_feedback_available;
    bool chemical_close_feedback_available;
    bool ro_water_close_feedback_available;
    bool dryer_close_feedback_available;
    bool roof_home_feedback_available;
    bool roof_home_reached;
    bool runtime_snapshot_read_should_fail;
    bool stop_all_should_fail;
    bool roof_home_command_should_fail;
    bool chemical_set_command_should_fail;
    bool chemical_stop_command_should_fail;
    bool roof_stop_command_should_fail;
    bool side_stop_command_should_fail;
    bool ro_water_close_command_should_fail;
    bool dryer_close_command_should_fail;
    int motion_command_count;
    int roof_follow_command_count;
    int side_brush_command_count;
    int chemical_command_count;
    int ro_water_command_count;
    int dryer_command_count;
    int roof_stop_command_count;
    int side_stop_command_count;
    int chemical_stop_command_count;
    int ro_water_close_command_count;
    int dryer_close_command_count;
    int roof_home_command_count;
    int stop_all_command_count;
} simulated_driver_context_t;

void simulated_driver_context_init(simulated_driver_context_t *driver_context);

#endif
