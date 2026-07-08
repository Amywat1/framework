/**
 * @file    test_engine_config.c
 * @brief   JSON 配置加载与 schema 校验单元测试（加载真实 m8 方案 JSON）
 * @author  huwangwei
 * @date    2026-06-26
 *
 * @note    方案源文件是 YAML，构建期由 tools/yaml2json.py 转为 JSON；
 *          本测试验证设备运行期使用的 JSON 加载器（cJSON）。
 */

#include "framework/adapters/outbound/storage/json/engine_program_json.h"
#include "framework/adapters/outbound/hal/sim/engine_io_sim.h"
#include "framework/domain/wash/model/engine_model.h"
#include "framework/domain/wash/engine/engine_expr.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M8_CONFIG_PATH
#define M8_CONFIG_PATH "doc/m8_normal_wash_program.json"
#endif

static const engine_lane_t *find_lane(const engine_phase_t *ph, const char *id)
{
    for (unsigned i = 0U; i < ph->lane_count; ++i)
    {
        if (strcmp(ph->lanes[i].id, id) == 0) { return &ph->lanes[i]; }
    }
    return NULL;
}

static const engine_step_t *find_step(const engine_lane_t *ln, const char *id)
{
    for (unsigned i = 0U; i < ln->step_count; ++i)
    {
        if (strcmp(ln->steps[i].id, id) == 0) { return &ln->steps[i]; }
    }
    return NULL;
}

static double __attribute__((unused)) find_param(const engine_program_t *p, const char *name)
{
    for (unsigned i = 0U; i < p->param_count; ++i)
    {
        if (strcmp(p->params[i].name, name) == 0) { return p->params[i].value; }
    }
    TEST_FAIL_MESSAGE("param not found");
    return 0.0;
}

static const engine_interlock_t *find_ilk(const engine_program_t *p, const char *id)
{
    for (unsigned i = 0U; i < p->interlock_count; ++i)
    {
        if (strcmp(p->interlocks[i].id, id) == 0) { return &p->interlocks[i]; }
    }
    return NULL;
}

typedef struct { double gantry_pos; bool tail_valid; double tail_pos; int fwd_limit; } eval_ctx_t;

static bool resolver(void *ctx, const char *name, double *out)
{
    const eval_ctx_t *e = (const eval_ctx_t *)ctx;
    if (strcmp(name, "axes.gantry.position") == 0)       { *out = e->gantry_pos; return true; }
    if (strcmp(name, "markers.car_tail.valid") == 0)     { *out = e->tail_valid ? 1.0 : 0.0; return true; }
    if (strcmp(name, "markers.car_tail.position") == 0)  { *out = e->tail_pos; return true; }
    if (strcmp(name, "GANTRY_FWD_LIMIT") == 0)           { *out = (double)e->fwd_limit; return true; }
    return false;
}

void setUp(void)
{
    engine_io_sim_register();
}

void tearDown(void) {}

static void test_load_structure(void)
{
    char err[160] = { 0 };
    engine_program_t *p = engine_program_load_json_file(M8_CONFIG_PATH, err, sizeof(err));
    if (p == NULL) { printf("  解析失败: %s\n", err); }
    TEST_ASSERT_NOT_NULL(p);

    TEST_ASSERT_EQUAL_STRING("1.0", p->schema_version);
    TEST_ASSERT_EQUAL_STRING("m8_normal_wash", p->id);

    /* 参数 */
    TEST_ASSERT_EQUAL_UINT(3U, p->param_count);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 150.0, find_param(p, "gantry_tail_min_pos"));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9,  60.0, find_param(p, "gantry_side_stop_offset"));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 150.0, find_param(p, "gantry_rear_near_pos"));

    /* 坐标轴 */
    TEST_ASSERT_EQUAL_UINT(1U, p->axis_count);
    TEST_ASSERT_EQUAL_STRING("gantry", p->axes[0].id);
    TEST_ASSERT_EQUAL_STRING("GANTRY_ENCODER_PULSE", p->axes[0].encoder);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, p->axes[0].pulse_per_mm);

    /* 标记（验证 on 键未被 YAML 1.1 误判为布尔） */
    TEST_ASSERT_EQUAL_UINT(1U, p->marker_count);
    TEST_ASSERT_EQUAL_STRING("car_tail", p->markers[0].id);
    TEST_ASSERT_EQUAL_STRING("gantry", p->markers[0].axis);
    TEST_ASSERT_EQUAL_STRING("RADAR_CAR_TAIL", p->markers[0].signal);
    TEST_ASSERT_EQUAL_INT(ENGINE_EDGE_RISING, p->markers[0].edge);

    /* 联锁 */
    TEST_ASSERT_EQUAL_UINT(4U, p->interlock_count);
    const engine_interlock_t *estop = find_ilk(p, "estop");
    TEST_ASSERT_NOT_NULL(estop);
    TEST_ASSERT_EQUAL_INT(ENGINE_ILK_HALT_ALL, estop->action);
    TEST_ASSERT_EQUAL_INT(0, estop->priority);
    TEST_ASSERT_FALSE(estop->auto_reset);
    TEST_ASSERT_NOT_NULL(estop->condition);
    TEST_ASSERT_NOT_NULL(estop->reset_condition);

    /* 阶段 */
    TEST_ASSERT_EQUAL_UINT(8U, p->phase_count);
    TEST_ASSERT_EQUAL_STRING("prepare",          p->phases[0].id);
    TEST_ASSERT_EQUAL_STRING("pass1_fwd_foam",   p->phases[1].id);
    TEST_ASSERT_EQUAL_STRING("homing",           p->phases[7].id);

    TEST_ASSERT_EQUAL_INT(ENGINE_DIR_NONE, p->phases[0].direction);
    TEST_ASSERT_EQUAL_UINT(0U, p->phases[0].on_exit_count);
    TEST_ASSERT_EQUAL_UINT(1U, p->phases[0].lane_count);
    const engine_step_t *tbs = find_step(find_lane(&p->phases[0], "top_brush_lane"), "top_brush_start");
    TEST_ASSERT_NOT_NULL(tbs);
    TEST_ASSERT_EQUAL_INT(ENGINE_TRIG_CONDITION, tbs->trigger.type);
    TEST_ASSERT_EQUAL_UINT(2U, tbs->action_count);
    TEST_ASSERT_EQUAL_INT(ENGINE_DONE_ACTIONS_COMPLETE, tbs->done.type);

    const engine_phase_t *pass1 = &p->phases[1];
    TEST_ASSERT_EQUAL_INT(ENGINE_DIR_FORWARD, pass1->direction);
    TEST_ASSERT_EQUAL_UINT(6U, pass1->on_exit_count);
    TEST_ASSERT_EQUAL_UINT(4U, pass1->lane_count);

    const engine_step_t *gantry_fwd =
        find_step(find_lane(pass1, "gantry_lane"), "gantry_fwd_ctrl");
    TEST_ASSERT_NOT_NULL(gantry_fwd);
    TEST_ASSERT_EQUAL_INT(ENGINE_STEP_CONTROL, gantry_fwd->type);
    TEST_ASSERT_NOT_NULL(gantry_fwd->active_while);
    TEST_ASSERT_NOT_NULL(gantry_fwd->value_expr);
    TEST_ASSERT_EQUAL_STRING("GANTRY_FWD", gantry_fwd->output);

    const engine_step_t *foam_off =
        find_step(find_lane(pass1, "foam_lane"), "foam_off_at_tail");
    TEST_ASSERT_NOT_NULL(foam_off);
    TEST_ASSERT_EQUAL_INT(ENGINE_TRIG_SIGNAL, foam_off->trigger.type);
    TEST_ASSERT_EQUAL_STRING("RADAR_CAR_TAIL", foam_off->trigger.signal);
    TEST_ASSERT_EQUAL_INT(ENGINE_EDGE_RISING, foam_off->trigger.edge);
    TEST_ASSERT_NOT_NULL(foam_off->guard);
    TEST_ASSERT_EQUAL_UINT(1U, foam_off->after_count);
    TEST_ASSERT_EQUAL_STRING("foam_on", foam_off->after[0]);

    const engine_step_t *lift =
        find_step(find_lane(pass1, "lifter_lane"), "lifter_to_up");
    TEST_ASSERT_NOT_NULL(lift);
    TEST_ASSERT_EQUAL_INT(ENGINE_DONE_SIGNAL, lift->done.type);
    TEST_ASSERT_EQUAL_STRING("LIFT_UP_LIMIT", lift->done.signal);
    TEST_ASSERT_EQUAL_INT(1, lift->done.state);
    TEST_ASSERT_EQUAL_UINT32(10000U, lift->done.timeout_ms);

    const engine_step_t *hpb =
        find_step(find_lane(&p->phases[2], "highpres_bottom_lane"), "highpres_bottom_on");
    TEST_ASSERT_NOT_NULL(hpb);
    TEST_ASSERT_EQUAL_UINT(2U, hpb->action_count);
    TEST_ASSERT_EQUAL_INT(ENGINE_ACT_WAIT_TIME, hpb->actions[0].type);
    TEST_ASSERT_EQUAL_UINT32(8000U, hpb->actions[0].ms);
    TEST_ASSERT_EQUAL_INT(ENGINE_ACT_IO_SET, hpb->actions[1].type);
    TEST_ASSERT_EQUAL_STRING("WATER_HIGHPRES_BOTTOM", hpb->actions[1].channel);

    const engine_step_t *tbm =
        find_step(find_lane(&p->phases[4], "top_brush_lane"), "top_brush_medium");
    TEST_ASSERT_NOT_NULL(tbm);
    TEST_ASSERT_EQUAL_INT(2, tbm->actions[0].value);  /* 整数 DO */

    /* pass5 exit_guard 语义验证 */
    const engine_phase_t *pass5 = &p->phases[5];
    TEST_ASSERT_NOT_NULL(pass5->exit_guard);
    {
        eval_ctx_t c1 = { 100.0, false, 0.0, 1 };
        engine_expr_env_t env = { resolver, &c1 };
        bool ok = false;
        TEST_ASSERT_TRUE(engine_expr_eval_bool(pass5->exit_guard, &env, &ok));
        TEST_ASSERT_TRUE(ok);

        eval_ctx_t c2 = { 250.0, true, 200.0, 0 };
        env.ctx = &c2;
        TEST_ASSERT_TRUE(engine_expr_eval_bool(pass5->exit_guard, &env, &ok));
        TEST_ASSERT_TRUE(ok);

        eval_ctx_t c3 = { 100.0, false, 0.0, 0 };
        env.ctx = &c3;
        TEST_ASSERT_FALSE(engine_expr_eval_bool(pass5->exit_guard, &env, &ok));
        TEST_ASSERT_TRUE(ok);
    }

    engine_program_free(p);
}

static void test_schema_rejects(void)
{
    char err[160];

    /* control 步骤缺少 value_expr */
    static const char *bad_control =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"t\",\"phases\":["
        "{\"id\":\"p\",\"entry_guard\":\"true\",\"exit_guard\":\"true\",\"timeout_ms\":1000,"
        "\"lanes\":[{\"id\":\"l\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"control\",\"active_while\":\"true\",\"output\":\"X\"}"
        "]}]}]}}";
    err[0] = '\0';
    TEST_ASSERT_NULL(engine_program_load_json_string(bad_control, err, sizeof(err)));
    TEST_ASSERT_TRUE(err[0] != '\0');

    /* 错误 schema_version */
    static const char *bad_ver =
        "{\"program\":{\"schema_version\":\"2.0\",\"id\":\"t\",\"phases\":["
        "{\"id\":\"p\",\"entry_guard\":\"true\",\"exit_guard\":\"true\",\"timeout_ms\":1000,"
        "\"lanes\":[{\"id\":\"l\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"done\":{\"type\":\"actions_complete\"}}"
        "]}]}]}}";
    TEST_ASSERT_NULL(engine_program_load_json_string(bad_ver, err, sizeof(err)));

    /* 缺少 phases */
    static const char *no_phases =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"t\"}}";
    TEST_ASSERT_NULL(engine_program_load_json_string(no_phases, err, sizeof(err)));

    /* 非法 JSON */
    static const char *bad_json = "{ not json ]";
    TEST_ASSERT_NULL(engine_program_load_json_string(bad_json, err, sizeof(err)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_structure);
    RUN_TEST(test_schema_rejects);
    return UNITY_END();
}
