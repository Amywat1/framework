#include "tests/test_support.h"

#include "domain/services/fault_policy_service.h"

int main(void)
{
    operation_result_t result;

    result = fault_policy_service_apply(FAULT_CLASS_RESOURCE, FAULT_POLICY_DEGRADE);
    TEST_ASSERT(result.ok);

    result = fault_policy_service_apply(FAULT_CLASS_SAFETY, FAULT_POLICY_DEGRADE);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_SAFETY_INTERLOCK_ACTIVE);
    return 0;
}

