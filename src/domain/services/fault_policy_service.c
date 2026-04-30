#include "domain/services/fault_policy_service.h"

#include "shared/error_codes.h"

operation_result_t fault_policy_service_apply(fault_class_t fault_class, fault_policy_t fault_policy)
{
    if (fault_class == FAULT_CLASS_SAFETY) {
        return operation_result_fail(ERROR_CODE_SAFETY_INTERLOCK_ACTIVE);
    }
    if (fault_policy == FAULT_POLICY_SAFE_FINISH || fault_policy == FAULT_POLICY_DEGRADE || fault_policy == FAULT_POLICY_SKIP) {
        return operation_result_ok();
    }
    return operation_result_fail(ERROR_CODE_UNSUPPORTED);
}

