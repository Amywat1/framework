#include "tests/support/adapter_error_contract.h"
#include "wdf_test_spec.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static sw_err_t passthrough_adapter(void *ctx, sw_err_t injected_error)
{
    (void)ctx;
    return injected_error;
}

static void test_adapter_error_contract(void)
{
    adapter_error_contract_fixture_t fixture = {
        .invoke = passthrough_adapter,
        .ctx    = NULL,
    };
    char err[160] = {0};

    TEST_ASSERT_EQUAL_INT(SW_OK, adapter_error_contract_run(&fixture, err, sizeof(err)));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_adapter_error_contract, "ERRM-04", "验证适配器硬件、通信、存储错误不被静默吞掉");
    return UNITY_END();
}
