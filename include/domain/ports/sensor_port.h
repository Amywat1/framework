#ifndef DOMAIN_PORTS_SENSOR_PORT_H
#define DOMAIN_PORTS_SENSOR_PORT_H

#include <stdbool.h>

#include "domain/model/position_trigger.h"

/**
 * @file sensor_port.h
 * @brief 定义平台层运行时事实读取端口。
 */
typedef struct actuator_feedback_snapshot_t {
    bool roof_brush_follow_ok;
    bool roof_brush_stopped_feedback_available;
    bool roof_brush_stopped;
    bool side_brush_stopped_feedback_available;
    bool side_brush_stopped;
    bool chemical_closed_feedback_available;
    bool chemical_closed;
    bool ro_water_closed_feedback_available;
    bool ro_water_closed;
    bool dryer_closed_feedback_available;
    bool dryer_closed;
    bool roof_brush_home_feedback_available;
    bool roof_brush_home_reached;
} actuator_feedback_snapshot_t;

typedef struct runtime_snapshot_t {
    position_snapshot_t position_snapshot;
    actuator_feedback_snapshot_t actuator_feedback;
} runtime_snapshot_t;

typedef struct sensor_port_t {
    void *context;
    int (*read_runtime_snapshot)(void *context, runtime_snapshot_t *runtime_snapshot);
} sensor_port_t;

#endif
