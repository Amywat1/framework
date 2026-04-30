#ifndef DOMAIN_SERVICES_RECOVERY_STATE_MACHINE_H
#define DOMAIN_SERVICES_RECOVERY_STATE_MACHINE_H

#include "domain/ports/actuator_port.h"
#include "shared/result_types.h"

operation_result_t recovery_state_machine_execute(const actuator_port_t *actuator_port);

#endif

