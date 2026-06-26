/**
 * @file    test_engine_runtime.c
 * @brief   引擎运行时状态机单元测试（sim IO 后端 + 小型 JSON 方案）
 * @author  huwangwei
 * @date    2026-06-26
 */

#include "domain/engine/engine.h"
#include "adapters/storage/json/engine_program_json.h"
#include "adapters/hal/sim_hw/engine_io_sim.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 装载一个方案字符串（JSON）并启动引擎 */
static engine_t *start_program(const char *json)
{
    engine_io_sim_reset();
    char err[160] = { 0 };
    engine_program_t *p = engine_program_load_json_string(json, err, sizeof(err));
    if (p == NULL) { printf("  解析失败: %s\n", err); }
    assert(p != NULL);

    engine_t *e = engine_create();
    assert(e != NULL);
    assert(engine_load_program(e, p) == SW_OK);
    assert(engine_start(e) == SW_OK);
    return e;
}

static void tick_n(engine_t *e, unsigned n, uint32_t dt)
{
    for (unsigned i = 0U; i < n; ++i) { engine_tick(e, dt); }
}

/* ---- wait_time + after + actions_complete ---- */
static void test_wait_and_after(void)
{
    printf("test_wait_and_after\n");
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tA\",\"phases\":["
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
    assert(engine_io_sim_get_output("AOUT") == 0);

    tick_n(e, 7U, 100U);
    assert(engine_io_sim_get_output("AOUT") == 1);
    assert(engine_io_sim_get_output("BOUT") == 0);

    tick_n(e, 25U, 100U);
    assert(engine_io_sim_get_output("BOUT") == 1);

    engine_destroy(e);
    printf("  PASS\n");
}

/* ---- signal rising 触发 ---- */
static void test_signal_edge(void)
{
    printf("test_signal_edge\n");
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tS\",\"phases\":["
        "{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":100000,"
        "\"lanes\":[{\"id\":\"l1\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"event\",\"trigger\":{\"type\":\"signal\",\"signal\":\"SIG\",\"edge\":\"rising\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"ROUT\",\"value\":1}}],\"done\":{\"type\":\"actions_complete\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    engine_io_sim_set_signal("SIG", 0);
    tick_n(e, 3U, 100U);
    assert(engine_io_sim_get_output("ROUT") == 0);

    engine_io_sim_set_signal("SIG", 1);
    tick_n(e, 1U, 100U);
    assert(engine_io_sim_get_output("ROUT") == 1);

    engine_destroy(e);
    printf("  PASS\n");
}

/* ---- 持续输出（trigger_exit）+ on_exit 清零 + 阶段推进 ---- */
static void test_trigger_exit_and_on_exit(void)
{
    printf("test_trigger_exit_and_on_exit\n");
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tT\",\"phases\":["
        "{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":100000,"
        "\"on_exit\":[{\"io_set\":{\"channel\":\"GOUT\",\"value\":0}}],"
        "\"lanes\":[{\"id\":\"g\",\"steps\":["
        "{\"id\":\"gstep\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"GOUT\",\"value\":1}}],\"done\":{\"type\":\"trigger_exit\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    tick_n(e, 3U, 100U);
    assert(engine_io_sim_get_output("GOUT") == 1);
    assert(engine_state(e) == ENGINE_STATE_RUNNING);

    engine_io_sim_set_signal("EXIT", 1);
    tick_n(e, 1U, 100U);
    assert(engine_io_sim_get_output("GOUT") == 0);
    assert(engine_state(e) == ENGINE_STATE_DONE);

    engine_destroy(e);
    printf("  PASS\n");
}

/* ---- 阶段串行 + done signal 超时 → on_error ---- */
static void test_phase_serial_and_done_timeout(void)
{
    printf("test_phase_serial_and_done_timeout\n");
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tP\",\"phases\":["
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
    assert(engine_io_sim_get_output("POUT") == 0);
    assert(engine_current_phase(e) == 0);

    tick_n(e, 8U, 100U);
    assert(engine_state(e) == ENGINE_STATE_RUNNING);
    assert(engine_current_phase(e) == 0);

    engine_io_sim_set_signal("EXIT0", 1);
    tick_n(e, 2U, 100U);
    assert(engine_current_phase(e) == 1);
    assert(engine_io_sim_get_output("POUT") == 1);
    assert(engine_io_sim_get_output("S1OUT") == 1);

    engine_destroy(e);
    printf("  PASS\n");
}

/* ---- 联锁 halt_all ---- */
static void test_interlock_halt_all(void)
{
    printf("test_interlock_halt_all\n");
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
    assert(engine_io_sim_get_output("XOUT") == 1);

    engine_io_sim_set_signal("ESTOP", 1);
    tick_n(e, 1U, 100U);
    assert(engine_state(e) == ENGINE_STATE_HALTED);
    assert(engine_io_sim_get_output("XOUT") == 0);

    engine_destroy(e);
    printf("  PASS\n");
}

/* ---- 联锁 halt_phase + recover ---- */
static void test_interlock_halt_phase(void)
{
    printf("test_interlock_halt_phase\n");
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tH\","
        "\"interlocks\":[{\"id\":\"coll\",\"condition\":\"HP == 1\",\"action\":\"halt_phase\","
        "\"priority\":1,\"reset_condition\":\"HP == 0\",\"auto_reset\":true}],"
        "\"phases\":[{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":100000,"
        "\"on_exit\":[{\"io_set\":{\"channel\":\"YOUT\",\"value\":0}}],"
        "\"lanes\":[{\"id\":\"l0\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"YOUT\",\"value\":1}}],\"done\":{\"type\":\"trigger_exit\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    tick_n(e, 2U, 100U);
    assert(engine_io_sim_get_output("YOUT") == 1);

    engine_io_sim_set_signal("HP", 1);
    tick_n(e, 1U, 100U);
    assert(engine_state(e) == ENGINE_STATE_PHASE_HALTED);
    assert(engine_io_sim_get_output("YOUT") == 0);

    engine_io_sim_set_signal("HP", 0);
    assert(engine_recover(e) == SW_OK);
    tick_n(e, 1U, 100U);
    assert(engine_state(e) == ENGINE_STATE_RUNNING);
    assert(engine_io_sim_get_output("YOUT") == 1);

    engine_destroy(e);
    printf("  PASS\n");
}

/* ---- 联锁 custom_action ---- */
static void test_interlock_custom(void)
{
    printf("test_interlock_custom\n");
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tC\","
        "\"interlocks\":[{\"id\":\"ca\",\"condition\":\"CA == 1\",\"action\":\"custom_action\","
        "\"actions\":[{\"io_set\":{\"channel\":\"COUT\",\"value\":5}}],"
        "\"priority\":2,\"reset_condition\":\"CA == 0\",\"auto_reset\":true}],"
        "\"phases\":[{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":100000,"
        "\"lanes\":[{\"id\":\"l0\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"actions\":[{\"io_set\":{\"channel\":\"ZOUT\",\"value\":1}}],\"done\":{\"type\":\"actions_complete\"}}"
        "]}]}]}}";
    engine_t *e = start_program(json);

    tick_n(e, 1U, 100U);
    assert(engine_io_sim_get_output("COUT") == 0);

    engine_io_sim_set_signal("CA", 1);
    tick_n(e, 1U, 100U);
    assert(engine_io_sim_get_output("COUT") == 5);
    assert(engine_state(e) == ENGINE_STATE_RUNNING);

    engine_io_sim_set_signal("CA", 0);
    tick_n(e, 1U, 100U);
    engine_io_sim_set_output("COUT", 0);
    engine_io_sim_set_signal("CA", 1);
    tick_n(e, 1U, 100U);
    assert(engine_io_sim_get_output("COUT") == 5);

    engine_destroy(e);
    printf("  PASS\n");
}

/* ---- 标记锁存 + 写一次保护 ---- */
static void test_marker_latch_once(void)
{
    printf("test_marker_latch_once\n");
    static const char *json =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"tM\","
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

    assert(engine_state(e) == ENGINE_STATE_DONE);

    engine_destroy(e);
    printf("  PASS\n");
}

int main(void)
{
    printf("=== test_engine_runtime ===\n");
    engine_io_sim_register();

    test_wait_and_after();
    test_signal_edge();
    test_trigger_exit_and_on_exit();
    test_phase_serial_and_done_timeout();
    test_interlock_halt_all();
    test_interlock_halt_phase();
    test_interlock_custom();
    test_marker_latch_once();

    printf("=== ALL PASSED ===\n");
    return 0;
}
