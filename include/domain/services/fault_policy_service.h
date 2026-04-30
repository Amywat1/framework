#ifndef DOMAIN_SERVICES_FAULT_POLICY_SERVICE_H
#define DOMAIN_SERVICES_FAULT_POLICY_SERVICE_H

#include "domain/model/domain_enums.h"
#include "shared/result_types.h"

operation_result_t fault_policy_service_apply(fault_class_t fault_class, fault_policy_t fault_policy);

#endif

