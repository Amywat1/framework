#ifndef DOMAIN_PORTS_ACTUATOR_PORT_H
#define DOMAIN_PORTS_ACTUATOR_PORT_H

#include "domain/model/domain_enums.h"

typedef struct actuator_port_t {
    void *context;
    int (*move_gantry)(void *context, gantry_motion_mode_t motion_mode, int traverse_count, int timeout_ms);
    int (*control_roof_brush)(void *context, brush_mode_t brush_mode, int timeout_ms);
    int (*control_side_brush)(void *context, brush_mode_t brush_mode, int timeout_ms);
    int (*dose_chemical)(void *context, const char *channel_id, int duration_ms, int retry_limit, int timeout_ms);
    int (*stop_all)(void *context, int timeout_ms);
} actuator_port_t;

#endif

