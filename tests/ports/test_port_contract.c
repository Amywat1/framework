/**
 * @file    test_port_contract.c
 * @brief   必需端口启动期校验单元测试
 * @author  HUWANGWEI
 * @date    2026-08-03
 */

#include "ports/inbound/command/command_port.h"
#include "ports/outbound/hal/hal_io_port.h"
#include "ports/outbound/hal/hal_voice_port.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "ports/port_contract.h"
#include "ports/port_registry.h"
#include "wdf_test_spec.h"

/* -------------------------------------------------------------------------
 * 测试替身：仅需非空 ops 让 get_ops 返回非 NULL
 * ------------------------------------------------------------------------- */
static sw_err_t fake_io_init(void)
{
    return SW_OK;
}
static sw_err_t fake_do_set(io_do_t pin, bool val)
{
    (void)pin;
    (void)val;
    return SW_OK;
}
static sw_err_t fake_di_read(io_di_t pin, io_di_sample_t *sample)
{
    (void)pin;
    (void)sample;
    return SW_OK;
}

static const hal_io_ops_t s_io_ops = {
    .init    = fake_io_init,
    .do_set  = fake_do_set,
    .di_read = fake_di_read,
};

static sw_err_t fake_submit(const dev_cmd_t *cmd, dev_cmd_receipt_t *receipt, uint32_t timeout_ms)
{
    (void)cmd;
    (void)receipt;
    (void)timeout_ms;
    return SW_OK;
}

static const device_command_port_ops_t s_cmd_ops = {.submit = fake_submit};

static const machine_ops_t s_machine_ops = {0};

void setUp(void)
{
    port_registry_hal_reset();
    port_registry_infra_reset();
    port_registry_cloud_reset();
}

void tearDown(void)
{
    port_registry_hal_reset();
    port_registry_infra_reset();
    port_registry_cloud_reset();
}

/* 未声明任何必需端口时直接通过，不因端口全空而失败 */
static void test_empty_requirement_passes(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, port_contract_validate(0U));
}

/* 声明的端口未注册时必须失败，避免缺失注册被推迟到首次业务调用才暴露 */
static void test_missing_port_fails(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, port_contract_validate(PORT_REQ_HAL_IO));
}

/* 声明的端口已注册时通过 */
static void test_registered_port_passes(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_register(&s_io_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, port_contract_validate(PORT_REQ_HAL_IO));
}

/* 多端口组合：只要有一项缺失即整体失败 */
static void test_partial_registration_fails(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_register(&s_io_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_register(&s_cmd_ops));

    /* machine_ops 未注册 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
                          port_contract_validate(PORT_REQ_HAL_IO | PORT_REQ_DEVICE_COMMAND | PORT_REQ_MACHINE_OPS));

    TEST_ASSERT_EQUAL_INT(SW_OK, machine_ops_register(&s_machine_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK,
                          port_contract_validate(PORT_REQ_HAL_IO | PORT_REQ_DEVICE_COMMAND | PORT_REQ_MACHINE_OPS));
}

/* 未声明的端口不参与校验：无云项目不应被要求注册 cloud 端口 */
static void test_undeclared_port_not_checked(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_register(&s_io_ops));

    /* 只声明 IO，cloud_link / param_store 等全部未注册也应通过 */
    TEST_ASSERT_EQUAL_INT(SW_OK, port_contract_validate(PORT_REQ_HAL_IO));
}

/* 解除注册后重新校验应失败，确认校验读的是实时状态而非缓存 */
static void test_unregister_makes_validation_fail(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_register(&s_io_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, port_contract_validate(PORT_REQ_HAL_IO));

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_register(NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, port_contract_validate(PORT_REQ_HAL_IO));
}

/* 含未知位时忽略该位，已声明的已知端口仍照常校验 */
static void test_unknown_bits_ignored(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_register(&s_io_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, port_contract_validate(PORT_REQ_HAL_IO | (1U << 30)));
}

/* 名称查询用于日志可读性 */
static void test_name_lookup(void)
{
    TEST_ASSERT_EQUAL_STRING("hal_io", port_contract_name(PORT_REQ_HAL_IO));
    TEST_ASSERT_EQUAL_STRING("machine_ops", port_contract_name(PORT_REQ_MACHINE_OPS));
    TEST_ASSERT_EQUAL_STRING("unknown", port_contract_name((port_requirement_t)(1U << 30)));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_empty_requirement_passes, "", "验证空需求通过");
    WDF_RUN_TEST(test_missing_port_fails, "", "验证缺失端口失败");
    WDF_RUN_TEST(test_registered_port_passes, "", "验证已注册端口通过");
    WDF_RUN_TEST(test_partial_registration_fails, "", "验证部分注册失败");
    WDF_RUN_TEST(test_undeclared_port_not_checked, "", "验证未声明端口未被检查");
    WDF_RUN_TEST(test_unregister_makes_validation_fail, "", "验证注销导致校验失败");
    WDF_RUN_TEST(test_unknown_bits_ignored, "", "验证未知位被忽略");
    WDF_RUN_TEST(test_name_lookup, "", "验证名称查询");
    return UNITY_END();
}
