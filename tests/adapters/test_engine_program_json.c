/**
 * @file    test_engine_program_json.c
 * @brief   engine program JSON loader + actuator sim 单元测试
 */

#include "adapters/outbound/hal/sim/engine_actuator_sim.h"
#include "adapters/outbound/hal/sim/engine_io_sim.h"
#include "adapters/outbound/storage/json/engine_program_json.h"
#include "common/sw_error.h"
#include "domain/program_engine/engine/engine.h"
#include "domain/program_engine/engine/engine_io.h"
#include "domain/program_engine/engine/engine_var.h"
#include "domain/program_engine/model/engine_model.h"
#include "domain/ports/outbound/storage/engine_program_loader_port.h"
#include "wdf_test_spec.h"

#include <stdio.h>
#include <string.h>

static bool s_tail_active;

static bool test_var_resolve(const char *name, double *out)
{
    if ((name == NULL) || (out == NULL) || (strcmp(name, "car_tail.active") != 0)) {
        return false;
    }
    *out = s_tail_active ? 1.0 : 0.0;
    return true;
}

static const char *const           s_test_var_names[] = {"car_tail.active"};
static const engine_var_provider_t s_test_vars        = {
           .resolve    = test_var_resolve,
           .names      = s_test_var_names,
           .name_count = 1U,
};

static const char *const s_program_json
    = "{"
      "\"program\":{"
      "\"schema_version\":\"1.0\","
      "\"id\":\"json_unit\","
      "\"name\":\"json unit\","
      "\"params\":{\"delay_ms\":200},"
      "\"axes\":{\"gantry\":{\"type\":\"physical\",\"encoder\":\"enc\",\"pulse_per_mm\":1,\"direction\":1}},"
      "\"markers\":{\"tail\":{\"type\":\"latch\",\"axis\":\"gantry\",\"on\":{\"signal\":\"TAIL\",\"edge\":\"rising\"}}}"
      ","
      "\"interlocks\":[{\"id\":\"estop\",\"condition\":\"ESTOP == 1\",\"action\":\"halt_all\","
      "\"priority\":0,\"reset_condition\":\"ESTOP == 0\",\"auto_reset\":false}],"
      "\"phases\":[{"
      "\"id\":\"p0\","
      "\"name\":\"phase\","
      "\"direction\":\"forward\","
      "\"entry_guard\":\"true\","
      "\"exit_guard\":\"EXIT == 1\","
      "\"timeout_ms\":10000,"
      "\"on_exit\":[{\"act\":{\"resource\":\"aout\",\"cmd\":\"stop\"}}],"
      "\"lanes\":[{\"id\":\"lane\",\"steps\":["
      "{\"id\":\"a\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"phase.elapsed_ms >= "
      "$delay_ms\"},"
      "\"actions\":[{\"act\":{\"resource\":\"aout\",\"cmd\":\"run\",\"gear\":1}}],\"done\":{\"type\":\"actions_"
      "complete\"}},"
      "{\"id\":\"b\",\"type\":\"event\",\"trigger\":{\"type\":\"signal\",\"signal\":\"SIG\",\"edge\":\"rising\"},"
      "\"after\":[\"a\"],\"actions\":[{\"wait_time\":{\"ms\":100}},{\"act\":{\"resource\":\"bout\",\"cmd\":\"run\","
      "\"gear\":1}}],"
      "\"done\":{\"type\":\"actions_complete\"}}"
      "]}]"
      "}]"
      "}"
      "}";

void setUp(void)
{
    engine_io_sim_register();
    engine_io_sim_reset();
    engine_actuator_sim_reset();
    engine_actuator_sim_register();
    engine_var_register(&s_test_vars);
    engine_set_pre_tick(NULL, NULL);
    s_tail_active = false;
}

void tearDown(void)
{
    engine_var_register(NULL);
    engine_set_pre_tick(NULL, NULL);
}

static void tick_n(engine_t *engine, unsigned count, uint32_t dt_ms)
{
    for (unsigned i = 0U; i < count; ++i) {
        engine_tick(engine, dt_ms);
    }
}

static void test_json_loader_parses_model_and_validates_catalog(void)
{
    char              err[200];
    engine_program_t *program = engine_program_load_json_string(s_program_json, err, sizeof(err));

    TEST_ASSERT_NOT_NULL_MESSAGE(program, err);
    TEST_ASSERT_EQUAL_STRING("json_unit", program->id);
    TEST_ASSERT_EQUAL_UINT(1U, program->param_count);
    TEST_ASSERT_EQUAL_STRING("delay_ms", program->params[0].name);
    TEST_ASSERT_EQUAL_UINT(1U, program->axis_count);
    TEST_ASSERT_EQUAL_STRING("gantry", program->axes[0].id);
    TEST_ASSERT_EQUAL_UINT(1U, program->marker_count);
    TEST_ASSERT_EQUAL_STRING("tail", program->markers[0].id);
    TEST_ASSERT_EQUAL_INT(ENGINE_MARKER_ON_SIGNAL, program->markers[0].on_kind);
    TEST_ASSERT_EQUAL_UINT(1U, program->interlock_count);
    TEST_ASSERT_EQUAL_UINT(1U, program->phase_count);
    TEST_ASSERT_EQUAL_INT(ENGINE_DIR_FORWARD, program->phases[0].direction);
    TEST_ASSERT_EQUAL_UINT(2U, program->phases[0].lanes[0].step_count);

    engine_program_free(program);
}

static void test_json_loader_rejects_unknown_resource(void)
{
    static const char *bad_json
        = "{"
          "\"program\":{"
          "\"schema_version\":\"1.0\","
          "\"id\":\"bad\","
          "\"interlocks\":[{\"id\":\"estop\",\"condition\":\"ESTOP == 1\",\"action\":\"halt_all\","
          "\"priority\":0,\"reset_condition\":\"ESTOP == 0\",\"auto_reset\":false}],"
          "\"phases\":[{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"true\",\"timeout_ms\":1000,"
          "\"lanes\":[{\"id\":\"lane\",\"steps\":[{\"id\":\"a\",\"type\":\"event\","
          "\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
          "\"actions\":[{\"act\":{\"resource\":\"unknown_res\",\"cmd\":\"run\",\"gear\":1}}],"
          "\"done\":{\"type\":\"actions_complete\"}}]}]}]"
          "}"
          "}";
    char err[200];

    TEST_ASSERT_NULL(engine_program_load_json_string(bad_json, err, sizeof(err)));
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(err));
}

static void test_engine_runs_loaded_json_with_sim_io(void)
{
    char              err[200];
    engine_t         *engine;
    engine_program_t *program = engine_program_load_json_string(s_program_json, err, sizeof(err));

    TEST_ASSERT_NOT_NULL_MESSAGE(program, err);
    engine = engine_create();
    TEST_ASSERT_NOT_NULL(engine);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_load_program(engine, program));
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_start(engine));

    engine_io_sim_set_axis("gantry", 42.0, 1.0, true);
    engine_io_sim_set_signal("SIG", 0);
    tick_n(engine, 3U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("aout"));
    TEST_ASSERT_EQUAL_INT(0, engine_actuator_sim_active("bout"));
    TEST_ASSERT_EQUAL_INT(42, (int)engine_io_sim_get_axis_pos("gantry"));

    engine_io_sim_set_signal("SIG", 1);
    tick_n(engine, 2U, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("bout"));

    engine_io_sim_set_signal("EXIT", 1);
    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_DONE, engine_state(engine));
    TEST_ASSERT_EQUAL_INT(0, engine_actuator_sim_active("aout"));

    engine_destroy(engine);
}

static void test_json_loader_expands_step_templates(void)
{
    static const char *templated_json
        = "{"
          "\"program\":{"
          "\"schema_version\":\"1.0\","
          "\"id\":\"templated\","
          "\"templates\":{"
          "\"always_on\":{\"type\":\"control\",\"active_while\":\"true\","
          "\"intent\":{\"resource\":\"aout\",\"cmd\":\"run\",\"gear\":1},"
          "\"on_error\":\"degrade\"}"
          "},"
          "\"interlocks\":[{\"id\":\"estop\",\"condition\":\"ESTOP == 1\",\"action\":\"halt_all\","
          "\"priority\":0,\"reset_condition\":\"ESTOP == 0\",\"auto_reset\":false}],"
          "\"phases\":[{\"id\":\"p0\",\"entry_guard\":\"true\",\"exit_guard\":\"EXIT == 1\",\"timeout_ms\":1000,"
          "\"lanes\":[{\"id\":\"lane\",\"steps\":["
          "{\"id\":\"templated_a\",\"use\":\"always_on\"},"
          "{\"id\":\"templated_b\",\"use\":\"always_on\",\"intent\":{\"resource\":\"bout\",\"cmd\":\"run\",\"gear\":2}}"
          "]}]}]"
          "}"
          "}";
    char              err[200];
    engine_t         *engine;
    engine_program_t *program = engine_program_load_json_string(templated_json, err, sizeof(err));

    TEST_ASSERT_NOT_NULL_MESSAGE(program, err);
    TEST_ASSERT_EQUAL_UINT(2U, program->phases[0].lanes[0].step_count);
    TEST_ASSERT_EQUAL_INT(ENGINE_STEP_CONTROL, program->phases[0].lanes[0].steps[0].type);
    TEST_ASSERT_EQUAL_STRING("aout", program->phases[0].lanes[0].steps[0].intent.resource);
    TEST_ASSERT_EQUAL_INT(ENGINE_ERR_DEGRADE, program->phases[0].lanes[0].steps[0].on_error);
    TEST_ASSERT_EQUAL_STRING("bout", program->phases[0].lanes[0].steps[1].intent.resource);
    TEST_ASSERT_EQUAL_INT(2, program->phases[0].lanes[0].steps[1].intent.gear);

    engine = engine_create();
    TEST_ASSERT_NOT_NULL(engine);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_load_program(engine, program));
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_start(engine));

    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("aout"));
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("bout"));
    TEST_ASSERT_EQUAL_INT(2, engine_actuator_sim_gear("bout"));

    engine_destroy(engine);
}

static void test_json_loader_port_registers_and_loads_file(void)
{
    const char       *path = "/tmp/wdf_engine_program_test.json";
    char              err[200];
    FILE             *fp = fopen(path, "wb");
    engine_program_t *program;

    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_UINT((unsigned)strlen(s_program_json),
                           (unsigned)fwrite(s_program_json, 1U, strlen(s_program_json), fp));
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));

    engine_program_json_register_loader();
    program = engine_program_load(path, err, sizeof(err));
    TEST_ASSERT_NOT_NULL_MESSAGE(program, err);
    TEST_ASSERT_EQUAL_STRING("json_unit", program->id);
    engine_program_free(program);
}

static void test_condition_marker_latches_on_rising(void)
{
    static const char *json
        = "{"
          "\"program\":{"
          "\"schema_version\":\"1.0\","
          "\"id\":\"cond_mk\","
          "\"axes\":{\"gantry\":{\"type\":\"physical\",\"encoder\":\"enc\",\"pulse_per_mm\":1,\"direction\":1}},"
          "\"markers\":{\"tail\":{\"type\":\"latch\",\"axis\":\"gantry\","
          "\"on\":{\"condition\":\"car_tail.active\",\"edge\":\"rising\"}}},"
          "\"interlocks\":[{\"id\":\"estop\",\"condition\":\"ESTOP == 1\",\"action\":\"halt_all\","
          "\"priority\":0,\"reset_condition\":\"ESTOP == 0\",\"auto_reset\":false}],"
          "\"phases\":[{\"id\":\"p0\",\"entry_guard\":\"true\","
          "\"exit_guard\":\"false\",\"timeout_ms\":10000,"
          "\"lanes\":[{\"id\":\"lane\",\"steps\":[{\"id\":\"a\",\"type\":\"event\","
          "\"trigger\":{\"type\":\"condition\",\"expr\":\"markers.tail.valid\"},"
          "\"actions\":[{\"act\":{\"resource\":\"aout\",\"cmd\":\"run\",\"gear\":1}}],"
          "\"done\":{\"type\":\"actions_complete\"}}]}]}]"
          "}"
          "}";
    char              err[200];
    engine_t         *engine;
    engine_program_t *program = engine_program_load_json_string(json, err, sizeof(err));

    TEST_ASSERT_NOT_NULL_MESSAGE(program, err);
    TEST_ASSERT_EQUAL_INT(ENGINE_MARKER_ON_CONDITION, program->markers[0].on_kind);
    TEST_ASSERT_NOT_NULL(program->markers[0].cond);

    engine = engine_create();
    TEST_ASSERT_NOT_NULL(engine);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_load_program(engine, program));
    engine_io_sim_set_axis("gantry", 123.0, 0.0, true);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_start(engine));

    engine_tick(engine, 10U);
    TEST_ASSERT_EQUAL_INT(0, engine_actuator_sim_active("aout"));

    s_tail_active = true;
    engine_tick(engine, 10U);
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("aout"));
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_RUNNING, engine_state(engine));

    engine_destroy(engine);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_json_loader_parses_model_and_validates_catalog, "", "验证JSON加载器解析模型并校验目录");
    WDF_RUN_TEST(test_json_loader_rejects_unknown_resource, "", "验证JSON加载器拒绝未知资源");
    WDF_RUN_TEST(test_engine_runs_loaded_json_with_sim_io, "", "验证程序引擎使用模拟 IO 运行已加载的 JSON");
    WDF_RUN_TEST(test_json_loader_expands_step_templates, "", "验证JSON加载器展开步骤模板");
    WDF_RUN_TEST(test_json_loader_port_registers_and_loads_file, "", "验证JSON加载器端口注册并加载文件");
    WDF_RUN_TEST(test_condition_marker_latches_on_rising, "", "验证条件标记在上升沿锁存");

    return UNITY_END();
}
