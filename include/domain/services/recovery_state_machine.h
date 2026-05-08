#ifndef DOMAIN_SERVICES_RECOVERY_STATE_MACHINE_H
#define DOMAIN_SERVICES_RECOVERY_STATE_MACHINE_H

#include "domain/ports/actuator_port.h"
#include "shared/result_types.h"

/**
 * @file recovery_state_machine.h
 * @brief 定义异常停机收口动作。
 */
operation_result_t recovery_state_machine_execute(const actuator_port_t *actuator_port);

#endif
