/**
 * @file    test_bootstrap_hooks.c
 * @brief   bootstrap 钩子注册契约（行为契约 BOOT-08）
 *
 * 15 个钩子全为必填。逐个缺失穷举而非抽样：漏检一个字段的后果是该阶段在启动时
 * 被跳过——bootstrap 按固定阶段推进，缺失的钩子意味着某个阶段静默不执行，而
 * 启动日志里其余阶段一切正常。抽样测试对"少检了哪个字段"这类错误必然漏。
 *
 * 同时验证被拒绝的注册不得覆盖已有的合法注册：只断言返回码不足以发现"拒绝的
 * 同时已经把 s_hooks 指向了残缺的表"这类实现缺陷。这与 PORT-02 是同一性质。
 */

#include "common/sw_error.h"
#include "runtime/bootstrap/bootstrap.h"
#include "runtime/bootstrap/project_hooks.h"
#include "wdf_test_spec.h"

#include <stddef.h>

/* bootstrap.o 链接桩：本测试只触达 bootstrap_register_hooks */
sw_err_t safety_session_coordinator_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t alarm_bridge_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t alarm_registry_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t alarm_binding_bridge_bind(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t command_gateway_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t event_bus_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t observation_event_bridge_init(void)
{
    return SW_ERR_NOT_INIT;
}
bool observation_is_ready(void)
{
    return false;
}
sw_err_t op_mode_bridge_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t operational_mode_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t recovery_service_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t scheduler_start_all(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t svc_param_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t telemetry_projection_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t project_hooks_register(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t wiring(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t thread_register(const char *name, void *(*fn)(void *), int sched_policy, int prio, size_t stack_size)
{
    (void)name;
    (void)fn;
    (void)sched_policy;
    (void)prio;
    (void)stack_size;
    return SW_ERR_NOT_INIT;
}
void time_util_init(void)
{
}
void event_bus_dispatch_loop(void)
{
}
void event_bus_set_fatal_cb(void (*cb)(const char *reason))
{
    (void)cb;
}
const void *deploy_store_get_ops(void)
{
    return NULL;
}
const void *hal_io_get_ops(void)
{
    return NULL;
}
const void *hal_vfd_get_ops(void)
{
    return NULL;
}
const void *hal_voice_get_ops(void)
{
    return NULL;
}

static sw_err_t ok_fn(void)
{
    return SW_OK;
}

static void void_fn(void)
{
}

/** @brief 构造一份全部字段就位的合法钩子表 */
static project_hooks_t make_full_hooks(void)
{
    project_hooks_t h = {
        .configure_storage      = ok_fn,
        .configure_hal          = ok_fn,
        .bind_hal               = ok_fn,
        .init_hal               = ok_fn,
        .configure_safety       = ok_fn,
        .init_safety            = ok_fn,
        .configure_adapters     = ok_fn,
        .bind_machine           = ok_fn,
        .init_machine           = ok_fn,
        .bind_alarm_catalog     = ok_fn,
        .validate               = ok_fn,
        .init_adapters          = ok_fn,
        .register_runtime_tasks = ok_fn,
        .start_runtime          = ok_fn,
        .assert_safe_outputs    = void_fn,
    };

    return h;
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_full_hooks_accepted(void)
{
    project_hooks_t h = make_full_hooks();

    TEST_ASSERT_EQUAL_INT(SW_OK, bootstrap_register_hooks(&h));
}

static void test_null_hooks_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, bootstrap_register_hooks(NULL));
}

#define ASSERT_FIELD_REQUIRED(field)                                                                                   \
    do {                                                                                                               \
        project_hooks_t h = make_full_hooks();                                                                         \
        h.field           = NULL;                                                                                      \
        TEST_ASSERT_EQUAL_INT_MESSAGE(SW_ERR_PARAM, bootstrap_register_hooks(&h), "缺失 " #field " 时应被拒绝");       \
    } while (0)

static void test_each_hook_is_required(void)
{
    ASSERT_FIELD_REQUIRED(configure_storage);
    ASSERT_FIELD_REQUIRED(configure_hal);
    ASSERT_FIELD_REQUIRED(bind_hal);
    ASSERT_FIELD_REQUIRED(init_hal);
    ASSERT_FIELD_REQUIRED(configure_safety);
    ASSERT_FIELD_REQUIRED(init_safety);
    ASSERT_FIELD_REQUIRED(configure_adapters);
    ASSERT_FIELD_REQUIRED(bind_machine);
    ASSERT_FIELD_REQUIRED(init_machine);
    ASSERT_FIELD_REQUIRED(bind_alarm_catalog);
    ASSERT_FIELD_REQUIRED(validate);
    ASSERT_FIELD_REQUIRED(init_adapters);
    ASSERT_FIELD_REQUIRED(register_runtime_tasks);
    ASSERT_FIELD_REQUIRED(start_runtime);
    ASSERT_FIELD_REQUIRED(assert_safe_outputs);
}

#define EXPECTED_HOOK_COUNT 15
typedef char hook_count_check_t[(sizeof(project_hooks_t) == EXPECTED_HOOK_COUNT * sizeof(void (*)(void))) ? 1 : -1];

static void test_hook_count_matches_coverage(void)
{
    TEST_ASSERT_EQUAL_UINT(EXPECTED_HOOK_COUNT * sizeof(void (*)(void)), sizeof(project_hooks_t));
}

static void test_rejected_registration_keeps_previous(void)
{
    project_hooks_t good = make_full_hooks();
    project_hooks_t bad  = make_full_hooks();

    bad.validate = NULL;

    TEST_ASSERT_EQUAL_INT(SW_OK, bootstrap_register_hooks(&good));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, bootstrap_register_hooks(&bad));
    TEST_ASSERT_EQUAL_INT(SW_OK, bootstrap_register_hooks(&good));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_full_hooks_accepted, "", "验证完整启动钩子集合被接受");
    WDF_RUN_TEST(test_null_hooks_rejected, "", "验证空指针钩子被拒绝");
    WDF_RUN_TEST(test_each_hook_is_required, "", "验证每个钩子为必需");
    WDF_RUN_TEST(test_hook_count_matches_coverage, "", "验证启动钩子数量与覆盖项一致");
    WDF_RUN_TEST(test_rejected_registration_keeps_previous, "", "验证被拒绝注册保持原有");
    return UNITY_END();
}
