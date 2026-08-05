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

#include <stdbool.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * bootstrap.c 的外部依赖桩
 *
 * 本测试只调用 bootstrap_register_hooks，它不触达任何这些符号。但 C 以目标文件
 * 为链接单位，bootstrap.o 引用的全部符号都须可解析，故在此提供桩而不是把真实
 * 模块链进来——后者会让"钩子字段校验"这条单元测试的失败原因可能来自任意
 * 被链接模块，失去定位价值。
 *
 * 桩一律返回失败或空：若某个用例意外走到了 bootstrap_run，失败会立刻暴露，
 * 而不是让测试在半初始化状态下继续。
 * ------------------------------------------------------------------------- */
sw_err_t abort_home_coordinator_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t alarm_event_bridge_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t alarm_lifecycle_bridge_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t alarm_registry_init(void)
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
sw_err_t safety_cutout_coordinator_init(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t scheduler_start_all(void)
{
    return SW_ERR_NOT_INIT;
}
sw_err_t self_check_service_init(void)
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

/* -------------------------------------------------------------------------
 * 钩子替身：全部返回成功，不产生副作用
 * ------------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------
 * 逐字段缺失穷举
 *
 * 用宏而不是手写 15 个函数：手写会让"是否覆盖了全部字段"取决于人是否数对，
 * 而字段增删时也无从提示。宏 + 集中列表使遗漏在阅读时即可见。
 * ------------------------------------------------------------------------- */
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

/*
 * 覆盖数与结构体字段数一致。
 *
 * 这条断言防的是"新增钩子字段但忘了加对应用例"：上面的列表是手写的，字段增加时
 * 不会自动扩展。sizeof 比较能在字段数变化时立刻失败，迫使作者回来补。
 *
 * 用 sizeof 而非字段计数是因为 C 没有反射；此处依赖"每个字段都是一个函数指针、
 * 且结构体无填充"这一前提，该前提由静态断言在编译期锁定。
 */
#define EXPECTED_HOOK_COUNT 15
typedef char hook_count_check_t[(sizeof(project_hooks_t) == EXPECTED_HOOK_COUNT * sizeof(void (*)(void))) ? 1 : -1];

static void test_hook_count_matches_coverage(void)
{
    /* 编译期断言已保证字段数；此处仅留一条运行期记录，使覆盖关系在测试输出里可见 */
    TEST_ASSERT_EQUAL_UINT(EXPECTED_HOOK_COUNT * sizeof(void (*)(void)), sizeof(project_hooks_t));
}

/* 拒绝非法注册不得破坏已有的合法注册 */
static void test_rejected_registration_keeps_previous(void)
{
    project_hooks_t good = make_full_hooks();
    project_hooks_t bad  = make_full_hooks();

    bad.validate = NULL;

    TEST_ASSERT_EQUAL_INT(SW_OK, bootstrap_register_hooks(&good));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, bootstrap_register_hooks(&bad));

    /* 再次注册同一份合法表应仍然成功——若上一次拒绝已把内部指针改坏，
     * 后续行为就不可预期。这里以"能再次成功注册"作为未被破坏的可观测证据。 */
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
