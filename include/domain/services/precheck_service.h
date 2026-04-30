#ifndef DOMAIN_SERVICES_PRECHECK_SERVICE_H
#define DOMAIN_SERVICES_PRECHECK_SERVICE_H

#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

operation_result_t precheck_service_run(const sensor_port_t *sensor_port);

#endif

