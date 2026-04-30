#ifndef DOMAIN_PORTS_SENSOR_PORT_H
#define DOMAIN_PORTS_SENSOR_PORT_H

#include <stdbool.h>

typedef struct sensor_snapshot_t {
    bool vehicle_present;
    bool vehicle_allowed;
    bool safety_ok;
    bool resource_ok;
    bool position_ok;
    bool estop_active;
} sensor_snapshot_t;

typedef struct sensor_port_t {
    void *context;
    int (*read_snapshot)(void *context, sensor_snapshot_t *snapshot);
} sensor_port_t;

#endif

