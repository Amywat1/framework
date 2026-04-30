#ifndef APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H
#define APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H

#include "domain/model/vehicle_type.h"
#include "domain/model/wash_cycle.h"
#include "domain/model/wash_program.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/event_logger_port.h"
#include "domain/ports/program_repository_port.h"
#include "domain/ports/sensor_port.h"

typedef struct system_context_t {
    wash_program_t wash_program;
    vehicle_type_t vehicle_type;
    wash_cycle_t wash_cycle;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    event_logger_port_t event_logger_port;
    program_repository_port_t program_repository_port;
} system_context_t;

#endif

