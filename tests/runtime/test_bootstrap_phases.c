/**
 * @file    test_bootstrap_phases.c
 * @brief   bootstrap 阶段推进与失败即止（行为契约 BOOT-01、BOOT-02、BOOT-03）
 *
 * 为何需要本测试：启动序列是框架最中心的编排逻辑（7 个阶段、20 余处 BOOT_CHECK），
 * 且**没有回滚路径**——`architecture/04` 第 4.1 节明确「失败即必须终止进程」。
 * 对这种无回滚的顺序状态机，最需要的恰恰是「任一阶段失败时后续阶段确实没跑」
 * 这条不变量：漏跑一个阶段不会报错，只会让设备带着半初始化状态运行。
 *
 * 此前 tests/runtime/test_bootstrap_hooks.c 只验注册期的 15 个字段必填，属于
 * 「进门前的参数校验」，不触达 bootstrap_run() 的任何阶段推进逻辑。BOOT-01~03
 * 三条因此长期是「待验证」。
 *
 * 做法：把 bootstrap 依赖的每个框架 init 都做成可编程桩，记录全局调用序列。
 * 逐个让某一步失败，断言两件事——
 *   1. bootstrap_run() 返回该步的错误码（BOOT-02）
 *   2. 其后的所有步骤一次都没被调用（BOOT-01、BOOT-03）
 *
 * 穷举每个失败点而非抽样：每个 BOOT_CHECK 都是一处独立的「失败是否真的中断」
 * 判断，漏测哪一处，那一处写成不检查返回值也不会被发现。
 */

#include "common/sw_error.h"
#include "runtime/bootstrap/bootstrap.h"
#include "runtime/bootstrap/project_hooks.h"
#include "runtime/bootstrap/wiring.h"
#include "wdf_test_spec.h"

/*
 * 桩函数须与真实声明签名一致，否则是未定义行为。故 include 被桩替换者的真实
 * 头文件，让编译器代为核对——尤其是 event_bus_set_fatal_cb 的回调类型
 * （event_bus_fatal_cb_t 带 reason + detail_code 两参）和四个 get_ops 的返回类型。
 */
#include "common/time_util.h"
#include "domain/ports/outbound/hal/hal_io_port.h"
#include "domain/ports/outbound/hal/hal_vfd_port.h"
#include "domain/ports/outbound/hal/hal_voice_port.h"
#include "domain/ports/outbound/storage/deploy_store.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/scheduler.h"
#include "runtime/scheduler/thread_registry.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 调用序列记录
 *
 * 用「步骤名 + 顺序」而不是布尔标志，因为要验的是顺序而非仅「是否被调用」。
 * ------------------------------------------------------------------------- */
#define MAX_STEPS 40

static const char *s_calls[MAX_STEPS];
static unsigned    s_call_count;

/* 被编程为失败的步骤名；NULL 表示全部成功 */
static const char *s_fail_step;
static sw_err_t    s_fail_code;

static sw_err_t step(const char *name)
{
    if (s_call_count < MAX_STEPS) {
        s_calls[s_call_count] = name;
        s_call_count++;
    }
    if ((s_fail_step != NULL) && (strcmp(name, s_fail_step) == 0)) {
        return s_fail_code;
    }
    return SW_OK;
}

/** @brief 某步骤是否被调用过 */
static bool was_called(const char *name)
{
    unsigned i;

    for (i = 0U; i < s_call_count; i++) {
        if (strcmp(s_calls[i], name) == 0) {
            return true;
        }
    }
    return false;
}

/** @brief 取某步骤的调用序号；未调用返回 -1 */
static int call_index(const char *name)
{
    unsigned i;

    for (i = 0U; i < s_call_count; i++) {
        if (strcmp(s_calls[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * 框架侧依赖桩
 * ------------------------------------------------------------------------- */
sw_err_t event_bus_init(void)
{
    return step("event_bus_init");
}
sw_err_t wiring(void)
{
    return step("wiring");
}
sw_err_t project_hooks_register(void)
{
    return step("project_hooks_register");
}
sw_err_t thread_register(const char *name, void *(*fn)(void *), int sched_policy, int prio, size_t stack_size)
{
    (void)name;
    (void)fn;
    (void)sched_policy;
    (void)prio;
    (void)stack_size;
    return step("thread_register");
}
sw_err_t svc_param_init(void)
{
    return step("svc_param_init");
}
sw_err_t alarm_registry_init(void)
{
    return step("alarm_registry_init");
}
sw_err_t alarm_bridge_bind(void)
{
    return step("alarm_bridge_bind");
}
sw_err_t alarm_bridge_init(void)
{
    return step("alarm_bridge_init");
}
sw_err_t operational_mode_init(void)
{
    return step("operational_mode_init");
}
sw_err_t command_gateway_init(void)
{
    return step("command_gateway_init");
}
sw_err_t recovery_service_init(void)
{
    return step("recovery_service_init");
}
sw_err_t safety_session_coordinator_init(void)
{
    return step("safety_session_coordinator_init");
}
sw_err_t op_mode_bridge_init(void)
{
    return step("op_mode_bridge_init");
}
sw_err_t telemetry_projection_init(void)
{
    return step("telemetry_projection_init");
}
sw_err_t scheduler_start_all(void)
{
    return step("scheduler_start_all");
}

void time_util_init(void)
{
}
void event_bus_dispatch_loop(void)
{
}
void event_bus_set_fatal_cb(event_bus_fatal_cb_t cb)
{
    (void)cb;
}

/* HAL 与存储端口一律未注册：本测试只关心阶段推进，端口缺失路径由
 * bootstrap 的 NULL 判断走「跳过」分支，不影响顺序断言。 */
const deploy_store_ops_t *deploy_store_get_ops(void)
{
    return NULL;
}
const hal_io_ops_t *hal_io_get_ops(void)
{
    return NULL;
}
const hal_vfd_ops_t *hal_vfd_get_ops(void)
{
    return NULL;
}
const hal_voice_ops_t *hal_voice_get_ops(void)
{
    return NULL;
}

/* -------------------------------------------------------------------------
 * 项目钩子桩
 * ------------------------------------------------------------------------- */
static sw_err_t hook_configure_storage(void)
{
    return step("configure_storage");
}
static sw_err_t hook_configure_hal(void)
{
    return step("configure_hal");
}
static sw_err_t hook_configure_safety(void)
{
    return step("configure_safety");
}
static sw_err_t hook_configure_adapters(void)
{
    return step("configure_adapters");
}
static sw_err_t hook_bind_hal(void)
{
    return step("bind_hal");
}
static sw_err_t hook_bind_device(void)
{
    return step("bind_device");
}
static sw_err_t hook_bind_alarm_catalog(void)
{
    return step("bind_alarm_catalog");
}
static sw_err_t hook_validate(void)
{
    return step("validate");
}
static sw_err_t hook_init_hal(void)
{
    return step("init_hal");
}
static sw_err_t hook_init_safety(void)
{
    return step("init_safety");
}
static sw_err_t hook_init_device(void)
{
    return step("init_device");
}
static sw_err_t hook_init_adapters(void)
{
    return step("init_adapters");
}
static sw_err_t hook_register_runtime_tasks(void)
{
    return step("register_runtime_tasks");
}
static sw_err_t hook_start_runtime(void)
{
    return step("start_runtime");
}
static void hook_assert_safe_outputs(void)
{
}

static project_hooks_t make_hooks(void)
{
    project_hooks_t h = {
        .configure_storage      = hook_configure_storage,
        .configure_hal          = hook_configure_hal,
        .bind_hal               = hook_bind_hal,
        .init_hal               = hook_init_hal,
        .configure_safety       = hook_configure_safety,
        .init_safety            = hook_init_safety,
        .configure_adapters     = hook_configure_adapters,
        .bind_device            = hook_bind_device,
        .init_device            = hook_init_device,
        .bind_alarm_catalog     = hook_bind_alarm_catalog,
        .validate               = hook_validate,
        .init_adapters          = hook_init_adapters,
        .register_runtime_tasks = hook_register_runtime_tasks,
        .start_runtime          = hook_start_runtime,
        .assert_safe_outputs    = hook_assert_safe_outputs,
    };

    return h;
}

/*
 * 全序：bootstrap_run() 成功时步骤的预期出现次序。
 * 失败注入用例据此断言「失败点之后一个都没跑」，因此这张表同时是 BOOT-01
 * 的顺序基线——阶段顺序被改动时，顺序用例会失败。
 */
static const char *const k_order[] = {
    /* register */
    "event_bus_init",
    "wiring",
    "project_hooks_register",
    "thread_register",
    /* load_storage */
    "configure_storage",
    "svc_param_init",
    /* configure */
    "configure_hal",
    "configure_safety",
    "configure_adapters",
    /* bind（含 validate） */
    "bind_hal",
    "bind_device",
    "alarm_registry_init",
    "alarm_bridge_bind",
    "bind_alarm_catalog",
    "validate",
    /* init_hal */
    "init_hal",
    "init_safety",
    "init_device",
    /* init_services */
    "alarm_bridge_init",
    "operational_mode_init",
    "command_gateway_init",
    "recovery_service_init",
    "safety_session_coordinator_init",
    "op_mode_bridge_init",
    "telemetry_projection_init",
    "init_adapters",
    "register_runtime_tasks",
    /* start */
    "start_runtime",
    "scheduler_start_all",
};

#define ORDER_COUNT ((unsigned)(sizeof(k_order) / sizeof(k_order[0])))

/*
 * 钩子表必须是静态存储：bootstrap_register_hooks() 只保存指针（bootstrap.c 的
 * s_hooks），随后 bootstrap_run() 会解引用它。用栈上的局部变量注册，函数返回后
 * s_hooks 即成悬垂指针。
 */
static project_hooks_t s_hooks_table;

static void register_hooks_once(void)
{
    s_hooks_table = make_hooks();
    TEST_ASSERT_EQUAL_INT(SW_OK, bootstrap_register_hooks(&s_hooks_table));
}

/** @brief 复位调用记录并编程失败点 */
static void arm(const char *fail_step, sw_err_t fail_code)
{
    memset(s_calls, 0, sizeof(s_calls));
    s_call_count = 0U;
    s_fail_step  = fail_step;
    s_fail_code  = fail_code;
}

void setUp(void)
{
    arm(NULL, SW_OK);
    register_hooks_once();
}

void tearDown(void)
{
}

/** BOOT-01：全部成功时，各步骤按固定顺序推进 */
static void test_phases_run_in_fixed_order(void)
{
    unsigned i;

    TEST_ASSERT_EQUAL_INT(SW_OK, bootstrap_run());

    for (i = 0U; i < ORDER_COUNT; i++) {
        TEST_ASSERT_TRUE_MESSAGE(was_called(k_order[i]), k_order[i]);
    }

    /* 相邻步骤的序号必须严格递增：只断言"都被调用过"无法发现顺序被调换 */
    for (i = 1U; i < ORDER_COUNT; i++) {
        TEST_ASSERT_TRUE_MESSAGE(call_index(k_order[i - 1U]) < call_index(k_order[i]), k_order[i]);
    }
}

/**
 * BOOT-02 与 BOOT-03：逐个注入失败，断言返回该错误码，且其后步骤全部未执行。
 *
 * 覆盖 k_order 的每一项——包含最后一项 scheduler_start_all（其后无步骤，
 * 只验返回码），使每个 BOOT_CHECK 都被独立验证过一次。
 */
static void test_failure_stops_subsequent_steps(void)
{
    unsigned f;

    for (f = 0U; f < ORDER_COUNT; f++) {
        unsigned later;

        arm(k_order[f], SW_ERR_STATE);

        TEST_ASSERT_EQUAL_INT_MESSAGE(SW_ERR_STATE, bootstrap_run(), k_order[f]);

        /* 失败步骤本身必须已被调用，否则说明注入没生效，用例是假通过 */
        TEST_ASSERT_TRUE_MESSAGE(was_called(k_order[f]), k_order[f]);

        for (later = f + 1U; later < ORDER_COUNT; later++) {
            TEST_ASSERT_FALSE_MESSAGE(was_called(k_order[later]), k_order[later]);
        }
    }
}

/**
 * BOOT-03 的关键子命题：无论在哪一步失败，scheduler_start_all 一定不被调用。
 *
 * 单列是因为它是「启动失败后不启动任何运行线程」这条契约的直接判据。线程一经
 * scheduler_start_all 创建即 detach 且无法回收，这一步跑了就再也收不回来。
 */
static void test_no_thread_start_on_any_failure(void)
{
    unsigned f;

    for (f = 0U; f < ORDER_COUNT - 1U; f++) {
        arm(k_order[f], SW_ERR_HW);

        TEST_ASSERT_EQUAL_INT(SW_ERR_HW, bootstrap_run());
        TEST_ASSERT_FALSE_MESSAGE(was_called("scheduler_start_all"), k_order[f]);
    }
}

/** BOOT-02：返回的是原始错误码，不被归一化成通用失败 */
static void test_original_error_code_propagates(void)
{
    static const sw_err_t codes[] = {SW_ERR_PARAM, SW_ERR_STATE, SW_ERR_HW, SW_ERR_NOT_INIT, SW_ERR_STORAGE};
    unsigned              i;

    for (i = 0U; i < (unsigned)(sizeof(codes) / sizeof(codes[0])); i++) {
        arm("validate", codes[i]);

        TEST_ASSERT_EQUAL_INT(codes[i], bootstrap_run());
    }
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_phases_run_in_fixed_order, "BOOT-01", "验证启动按固定阶段顺序推进");
    WDF_RUN_TEST(test_failure_stops_subsequent_steps, "BOOT-02", "验证任一步失败后续步骤全部不执行");
    WDF_RUN_TEST(test_no_thread_start_on_any_failure, "BOOT-03", "验证启动失败后不启动运行线程");
    WDF_RUN_TEST(test_original_error_code_propagates, "BOOT-02", "验证原始错误码被透传");
    return UNITY_END();
}
