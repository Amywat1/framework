/**
 * @file    test_alarm_binding_bridge.c
 * @brief   alarm_binding_bridge_bind 单元测试
 * @author  HUWANGWEI
 * @date    2026-08-08
 *
 * @note    绑定桥是唯一把入站 alarm_binding 接到 alarm_registry 的路径；
 *          注册成功后三个 ops 必须能转调 registry，否则 catalog 加载与触发会空转。
 */

#include "application/bridges/alarm_binding_bridge.h"
#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "common/sw_error.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/ports/port_registry.h"
#include "wdf_test_spec.h"

enum { TEST_ALARM_CODE = 201101U };

static const alarm_def_t s_catalog[] = {
    {
     .code         = TEST_ALARM_CODE,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "binding bridge",
     },
};

void setUp(void)
{
    port_registry_infra_reset();
    (void)alarm_registry_init();
}

void tearDown(void)
{
    port_registry_infra_reset();
}

/**
 * @brief  绑定后 get_ops 非空，且 trigger/clear/load_catalog 转调 registry
 */
static void test_bind_registers_ops_that_forward_to_registry(void)
{
    const alarm_binding_ops_t *ops = NULL;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_binding_bridge_bind());

    ops = alarm_binding_get_ops();
    TEST_ASSERT_NOT_NULL(ops);
    TEST_ASSERT_NOT_NULL(ops->trigger);
    TEST_ASSERT_NOT_NULL(ops->clear);
    TEST_ASSERT_NOT_NULL(ops->load_catalog);

    TEST_ASSERT_EQUAL_INT(SW_OK, ops->load_catalog(s_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, ops->trigger(TEST_ALARM_CODE));
    TEST_ASSERT_TRUE(alarm_registry_is_active(TEST_ALARM_CODE));

    TEST_ASSERT_EQUAL_INT(SW_OK, ops->clear(TEST_ALARM_CODE));
    /* MANUAL_RESET：clear 只消条件，锁存仍在直至复位；此处只断言转调成功且无崩溃 */
    TEST_ASSERT_TRUE(alarm_registry_is_active(TEST_ALARM_CODE));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_bind_registers_ops_that_forward_to_registry, "", "验证绑定桥注册入站 ops 并转调 alarm_registry");
    return UNITY_END();
}
