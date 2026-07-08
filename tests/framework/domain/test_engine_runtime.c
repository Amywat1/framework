/**
 * @file    test_engine_runtime.c
 * @brief   引擎运行时状态机单元测试（sim IO 后端 + 小型 JSON 方案）
 * @author  huwangwei
 * @date    2026-06-26
 */

#include "framework/domain/wash/engine/engine.h"
#include "framework/adapters/outbound/storage/json/engine_program_json.h"
#include "framework/adapters/outbound/hal/sim/engine_io_sim.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

void setUp(void)
{
    engine_io_sim_register();
    engine_io_sim_reset();
}

void tearDown(void) {}

#define ESTOP_JSON \
    "\"interlocks\":[{\"id\":\"estop\",\"condition\":\"ESTOP == 1\",\"action\":\"halt_all\"," \
    "\"priority\":0,\"reset_condition\":\"ESTOP == 0\",\"auto_reset\":false}],"

/* 装载一个方案字符串（JSON）并启动引擎 */
static engine_t *start_program(const char *json)
{
    engine_io_sim_reset();
    char err[160] = { 0 };
    engine_program_t *p = engine_program_load_json_string(json, err, sizeof(err));
    if (p == NULL) { printf("  解析失败: %s\n", err); }
    TEST_ASSERT_NOT_NULL(p);

    engine_t *e = engine_create();
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_load_program(e, p));
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_start(e));
    return e;
}

static void tick_n(engine_t *e, unsigned n, uint32_t dt)
{
    for (unsigned i = 0U; i < n; ++i) { engine_tick(e, dt); }
}

/* ---- wait_time + after + actions_complete ---- */
static void test_wait_and_after(void)
{
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tA\"," ESTOP_JSON "\"phases\":["
        "{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":100000,"
        "\"lanes\":[{\"id\":\"l1\",\"steps\":["
        "{\"id\":\"a\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"phase.elapsed_ms >= 1000\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"AOUT\",\"value\":1}}],\"done\":{\"type\":\"actions_complete\"}},"
        "{\"id\":\"b\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},\"after\":[\"a\"],"
        "\"actions\":[{\"wait_time\":{\"ms\":2000}},{\"io_set\":{\"channel\":\"BOUT\",\"value\":1}}],"
        "\"done\":{\"type\":\"actions_complete\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    tick_n(e, 5U, 100U);
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("AOUT"));

    tick_n(e, 7U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("AOUT"));
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("BOUT"));

    tick_n(e, 25U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("BOUT"));

    engine_destroy(e);
}

/* ---- signal rising 触发 ---- */
static void test_signal_edge(void)
{
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tS\"," ESTOP_JSON "\"phases\":["
        "{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":100000,"
        "\"lanes\":[{\"id\":\"l1\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"event\",\"trigger\":{\"type\":\"signal\",\"signal\":\"SIG\",\"edge\":\"rising\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"ROUT\",\"value\":1}}],\"done\":{\"type\":\"actions_complete\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    engine_io_sim_set_signal("SIG", 0);
    tick_n(e, 3U, 100U);
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("ROUT"));

    engine_io_sim_set_signal("SIG", 1);
    tick_n(e, 1U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("ROUT"));

    engine_destroy(e);
}

/* ---- 持续输出（trigger_exit）+ on_exit 清零 + 阶段推进 ---- */
static void test_trigger_exit_and_on_exit(void)
{
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tT\"," ESTOP_JSON "\"phases\":["
        "{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":100000,"
        "\"on_exit\":[{\"io_set\":{\"channel\":\"GOUT\",\"value\":0}}],"
        "\"lanes\":[{\"id\":\"g\",\"steps\":["
        "{\"id\":\"gstep\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"GOUT\",\"value\":1}}],\"done\":{\"type\":\"trigger_exit\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    tick_n(e, 3U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("GOUT"));
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_RUNNING, engine_state(e));

    engine_io_sim_set_signal("EXIT", 1);
    tick_n(e, 1U, 100U);
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("GOUT"));
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_DONE, engine_state(e));

    engine_destroy(e);
}

/* ---- 阶段串行 + done signal 超时 → on_error ---- */
static void test_phase_serial_and_done_timeout(void)
{
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tP\"," ESTOP_JSON "\"phases\":["
        "{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT0 == 1\",\"timeout_ms\":100000,"
        "\"lanes\":[{\"id\":\"l0\",\"steps\":["
        "{\"id\":\"s0\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"WAITSIG\",\"value\":1}}],"
        "\"done\":{\"type\":\"signal\",\"signal\":\"NEVER\",\"state\":1,\"timeout_ms\":500},\"on_error\":\"skip\"}"
        "]}]},"
        "{\"id\":\"p1\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT1 == 1\",\"timeout_ms\":100000,"
        "\"on_enter\":[{\"io_set\":{\"channel\":\"POUT\",\"value\":1}}],"
        "\"lanes\":[{\"id\":\"l1\",\"steps\":["
        "{\"id\":\"s1\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"S1OUT\",\"value\":1}}],\"done\":{\"type\":\"actions_complete\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    tick_n(e, 2U, 100U);
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("POUT"));
    TEST_ASSERT_EQUAL_INT(0, engine_current_phase(e));

    tick_n(e, 8U, 100U);
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_RUNNING, engine_state(e));
    TEST_ASSERT_EQUAL_INT(0, engine_current_phase(e));

    engine_io_sim_set_signal("EXIT0", 1);
    tick_n(e, 2U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_current_phase(e));
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("POUT"));
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("S1OUT"));

    engine_destroy(e);
}

/* ---- 联锁 halt_all ---- */
static void test_interlock_halt_all(void)
{
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tI\","
        "\"interlocks\":[{\"id\":\"estop\",\"condition\":\"ESTOP == 1\",\"action\":\"halt_all\","
        "\"priority\":0,\"reset_condition\":\"ESTOP == 0\",\"auto_reset\":false}],"
        "\"phases\":[{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":100000,"
        "\"lanes\":[{\"id\":\"l0\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"XOUT\",\"value\":1}}],\"done\":{\"type\":\"actions_complete\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    tick_n(e, 2U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("XOUT"));

    engine_io_sim_set_signal("ESTOP", 1);
    tick_n(e, 1U, 100U);
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_HALTED, engine_state(e));
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("XOUT"));

    engine_destroy(e);
}

/* ---- 联锁 halt_phase + recover ---- */
static void test_interlock_halt_phase(void)
{
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tH\","
        "\"interlocks\":[{\"id\":\"estop\",\"condition\":\"ESTOP == 1\",\"action\":\"halt_all\","
        "\"priority\":0,\"reset_condition\":\"ESTOP == 0\",\"auto_reset\":false},"
        "{\"id\":\"coll\",\"condition\":\"HP == 1\",\"action\":\"halt_phase\","
        "\"priority\":1,\"reset_condition\":\"HP == 0\",\"auto_reset\":true}],"
        "\"phases\":[{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":100000,"
        "\"on_exit\":[{\"io_set\":{\"channel\":\"YOUT\",\"value\":0}}],"
        "\"lanes\":[{\"id\":\"l0\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"YOUT\",\"value\":1}}],\"done\":{\"type\":\"trigger_exit\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    tick_n(e, 2U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("YOUT"));

    engine_io_sim_set_signal("HP", 1);
    tick_n(e, 1U, 100U);
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_PHASE_HALTED, engine_state(e));
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("YOUT"));

    engine_io_sim_set_signal("HP", 0);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_recover(e));
    tick_n(e, 1U, 100U);
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_RUNNING, engine_state(e));
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("YOUT"));

    engine_destroy(e);
}

/* ---- 联锁 custom_action ---- */
static void test_interlock_custom(void)
{
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tC\","
        "\"interlocks\":[{\"id\":\"estop\",\"condition\":\"ESTOP == 1\",\"action\":\"halt_all\","
        "\"priority\":0,\"reset_condition\":\"ESTOP == 0\",\"auto_reset\":false},"
        "{\"id\":\"ca\",\"condition\":\"CA == 1\",\"action\":\"custom_action\","
        "\"actions\":[{\"io_set\":{\"channel\":\"COUT\",\"value\":5}}],"
        "\"priority\":2,\"reset_condition\":\"CA == 0\",\"auto_reset\":true}],"
        "\"phases\":[{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":100000,"
        "\"lanes\":[{\"id\":\"l0\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"ZOUT\",\"value\":1}}],\"done\":{\"type\":\"actions_complete\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    tick_n(e, 1U, 100U);
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("COUT"));

    engine_io_sim_set_signal("CA", 1);
    tick_n(e, 1U, 100U);
    TEST_ASSERT_EQUAL_INT(5, engine_io_sim_get_output("COUT"));
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_RUNNING, engine_state(e));

    engine_io_sim_set_signal("CA", 0);
    tick_n(e, 1U, 100U);
    engine_io_sim_set_output("COUT", 0);
    engine_io_sim_set_signal("CA", 1);
    tick_n(e, 1U, 100U);
    TEST_ASSERT_EQUAL_INT(5, engine_io_sim_get_output("COUT"));

    engine_destroy(e);
}

/* ---- 标记锁存 + 写一次保护 ---- */
static void test_marker_latch_once(void)
{
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tM\"," ESTOP_JSON
        "\"axes\":{\"g\":{\"type\":\"physical\",\"encoder\":\"ENC\",\"pulse_per_mm\":1.0}},"
        "\"markers\":{\"tail\":{\"type\":\"latch\",\"axis\":\"g\","
        "\"on\":{\"signal\":\"TAIL\",\"edge\":\"rising\"}}},"
        "\"phases\":[{\"id\":\"p0\",\"entry_guard\":\"true\","
        "\"exit_guard\":\"markers.tail.valid AND markers.tail.position == 100\",\"timeout_ms\":5000,"
        "\"lanes\":[{\"id\":\"l0\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"NOP\",\"value\":1}}],\"done\":{\"type\":\"actions_complete\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    engine_io_sim_set_axis("g", 100.0, 0.0, true);
    engine_io_sim_set_signal("TAIL", 0);
    tick_n(e, 1U, 100U);
    engine_io_sim_set_signal("TAIL", 1);
    tick_n(e, 1U, 100U);

    engine_io_sim_set_axis("g", 200.0, 0.0, true);
    engine_io_sim_set_signal("TAIL", 0);
    tick_n(e, 1U, 100U);
    engine_io_sim_set_signal("TAIL", 1);
    tick_n(e, 2U, 100U);

    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_DONE, engine_state(e));

    engine_destroy(e);
}

/* ---- control：暂停解除后自动恢复输出 ---- */
static void test_control_pause_resume(void)
{
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tC\","
        "\"interlocks\":[{\"id\":\"estop\",\"condition\":\"ESTOP == 1\",\"action\":\"halt_all\","
        "\"priority\":0,\"reset_condition\":\"ESTOP == 0\",\"auto_reset\":false}],"
        "\"phases\":[{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\","
        "\"timeout_ms\":100000,"
        "\"lanes\":[{\"id\":\"g\",\"steps\":["
        "{\"id\":\"fwd\",\"type\":\"control\","
        "\"active_while\":\"GANTRY_PAUSE_REQUEST == 0 AND GANTRY_FWD_LIMIT == 0\","
        "\"output\":\"GANTRY_FWD\",\"value_expr\":\"1\",\"on_error\":\"halt_phase\"}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    tick_n(e, 2U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("GANTRY_FWD"));

    engine_io_sim_set_signal("GANTRY_PAUSE_REQUEST", 1);
    tick_n(e, 1U, 100U);
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("GANTRY_FWD"));

    engine_io_sim_set_signal("GANTRY_PAUSE_REQUEST", 0);
    tick_n(e, 1U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_io_sim_get_output("GANTRY_FWD"));

    engine_destroy(e);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wait_and_after);
    RUN_TEST(test_signal_edge);
    RUN_TEST(test_trigger_exit_and_on_exit);
    RUN_TEST(test_phase_serial_and_done_timeout);
    RUN_TEST(test_interlock_halt_all);
    RUN_TEST(test_interlock_halt_phase);
    RUN_TEST(test_interlock_custom);
    RUN_TEST(test_marker_latch_once);
    RUN_TEST(test_control_pause_resume);
    return UNITY_END();
}
