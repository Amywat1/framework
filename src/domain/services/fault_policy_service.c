#include "domain/services/fault_policy_service.h"

#include "shared/error_codes.h"

operation_result_t fault_policy_service_resolve(exception_strategy_t exception_strategy,
    bool currently_exiting,
    fault_policy_decision_t *fault_policy_decision)
{
    if (fault_policy_decision == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    fault_policy_decision->abort_session = false;
    fault_policy_decision->enter_exit = false;
    if (exception_strategy == EXCEPTION_STRATEGY_SAFE_FINISH && !currently_exiting) {
        fault_policy_decision->enter_exit = true;
        return operation_result_ok();
    }
    fault_policy_decision->abort_session = true;
    return operation_result_ok();
}
