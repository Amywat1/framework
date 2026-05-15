#ifndef DOMAIN_PORTS_ACTUATOR_PORT_H
#define DOMAIN_PORTS_ACTUATOR_PORT_H

#include <stdbool.h>

#include "domain/model/segment_motion_plan.h"

/**
 * @file actuator_port.h
 * @brief 定义平台层执行机构端口。
 */
typedef struct actuator_port_t
{
    void *context;
    int (*start_motion)(void *context, const segment_motion_plan_t *segment_motion_plan, int timeout_ms);
    int (*set_roof_brush_follow)(void *context, bool enabled, int timeout_ms);
    int (*set_side_brush_enabled)(void *context, bool enabled, int timeout_ms);
    int (*set_chemical_enabled)(void *context, bool enabled, int timeout_ms);
    int (*set_ro_water_enabled)(void *context, bool enabled, int timeout_ms);
    int (*set_dryer_enabled)(void *context, bool enabled, int timeout_ms);
    int (*stop_roof_brush)(void *context, int timeout_ms);
    int (*stop_side_brush)(void *context, int timeout_ms);
    int (*stop_chemical)(void *context, int timeout_ms);
    int (*close_ro_water)(void *context, int timeout_ms);
    int (*close_dryer)(void *context, int timeout_ms);
    int (*home_roof_brush)(void *context, int timeout_ms);
    int (*stop_all)(void *context, int timeout_ms);
} actuator_port_t;

#endif
