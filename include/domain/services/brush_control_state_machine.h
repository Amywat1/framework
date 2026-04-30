#ifndef DOMAIN_SERVICES_BRUSH_CONTROL_STATE_MACHINE_H
#define DOMAIN_SERVICES_BRUSH_CONTROL_STATE_MACHINE_H

#include "domain/model/wash_stage.h"
#include "domain/ports/actuator_port.h"
#include "shared/result_types.h"

operation_result_t brush_control_state_machine_execute(const actuator_port_t *actuator_port, const wash_stage_t *wash_stage);

#endif

