/**
 * @file    test_engine_config.c
 * @brief   JSON 配置加载与 schema 校验单元测试（加载真实 m8 方案 JSON）
 * @author  huwangwei
 * @date    2026-06-26
 *
 * @note    方案源文件是 YAML，构建期由 tools/yaml2json.py 转为 JSON；
 *          本测试验证设备运行期使用的 JSON 加载器（cJSON）。
 */

#include "adapters/storage/json/engine_program_json.h"
#include "domain/engine/engine_model.h"
#include "domain/engine/engine_expr.h"

#include <assert.h>
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

static double find_param(const engine_program_t *p, const char *name)
{
    for (unsigned i = 0U; i < p->param_count; ++i)
    {
        if (strcmp(p->params[i].name, name) == 0) { return p->params[i].value; }
    }
    assert(0 && "param not found");
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

static void test_load_structure(void)
{
    printf("test_load_structure\n");
    char err[160] = { 0 };
    engine_program_t *p = engine_program_load_json_file(M8_CONFIG_PATH, err, sizeof(err));
    if (p == NULL) { printf("  解析失败: %s\n", err); }
    assert(p != NULL);

    assert(strcmp(p->schema_version, "1.0") == 0);
    assert(strcmp(p->id, "m8_normal_wash") == 0);

    /* 参数 */
    assert(p->param_count == 3U);
    assert(fabs(find_param(p, "gantry_tail_min_pos") - 150.0) < 1e-9);
    assert(fabs(find_param(p, "gantry_side_stop_offset") - 60.0) < 1e-9);
    assert(fabs(find_param(p, "gantry_rear_near_pos") - 150.0) < 1e-9);

    /* 坐标轴 */
    assert(p->axis_count == 1U);
    assert(strcmp(p->axes[0].id, "gantry") == 0);
    assert(strcmp(p->axes[0].encoder, "GANTRY_ENCODER_PULSE") == 0);
    assert(fabs(p->axes[0].pulse_per_mm - 1.0) < 1e-9);

    /* 标记（验证 on 键未被 YAML 1.1 误判为布尔） */
    assert(p->marker_count == 1U);
    assert(strcmp(p->markers[0].id, "car_tail") == 0);
    assert(strcmp(p->markers[0].axis, "gantry") == 0);
    assert(strcmp(p->markers[0].signal, "RADAR_CAR_TAIL") == 0);
    assert(p->markers[0].edge == ENGINE_EDGE_RISING);

    /* 联锁 */
    assert(p->interlock_count == 5U);
    const engine_interlock_t *estop = find_ilk(p, "estop");
    assert(estop != NULL);
    assert(estop->action == ENGINE_ILK_HALT_ALL);
    assert(estop->priority == 0);
    assert(estop->auto_reset == false);
    assert(estop->condition != NULL);
    assert(estop->reset_condition != NULL);

    const engine_interlock_t *slope = find_ilk(p, "gantry_pause_slope");
    assert(slope != NULL);
    assert(slope->action == ENGINE_ILK_CUSTOM);
    assert(slope->action_count == 2U);
    assert(slope->auto_reset == true);
    assert(slope->actions[0].type == ENGINE_ACT_IO_SET);
    assert(strcmp(slope->actions[0].channel, "GANTRY_FWD") == 0);

    /* 阶段 */
    assert(p->phase_count == 8U);
    assert(strcmp(p->phases[0].id, "prepare") == 0);
    assert(strcmp(p->phases[1].id, "pass1_fwd_foam") == 0);
    assert(strcmp(p->phases[7].id, "homing") == 0);

    assert(p->phases[0].direction == ENGINE_DIR_NONE);
    assert(p->phases[0].on_exit_count == 0U);
    assert(p->phases[0].lane_count == 1U);
    const engine_step_t *tbs = find_step(find_lane(&p->phases[0], "top_brush_lane"), "top_brush_start");
    assert(tbs != NULL);
    assert(tbs->trigger.type == ENGINE_TRIG_CONDITION);
    assert(tbs->action_count == 2U);
    assert(tbs->done.type == ENGINE_DONE_ACTIONS_COMPLETE);

    const engine_phase_t *pass1 = &p->phases[1];
    assert(pass1->direction == ENGINE_DIR_FORWARD);
    assert(pass1->on_exit_count == 6U);
    assert(pass1->lane_count == 4U);

    const engine_step_t *foam_off =
        find_step(find_lane(pass1, "foam_lane"), "foam_off_at_tail");
    assert(foam_off != NULL);
    assert(foam_off->trigger.type == ENGINE_TRIG_SIGNAL);
    assert(strcmp(foam_off->trigger.signal, "RADAR_CAR_TAIL") == 0);
    assert(foam_off->trigger.edge == ENGINE_EDGE_RISING);
    assert(foam_off->guard != NULL);
    assert(foam_off->after_count == 1U);
    assert(strcmp(foam_off->after[0], "foam_on") == 0);

    const engine_step_t *lift =
        find_step(find_lane(pass1, "lifter_lane"), "lifter_to_up");
    assert(lift != NULL);
    assert(lift->done.type == ENGINE_DONE_SIGNAL);
    assert(strcmp(lift->done.signal, "LIFT_UP_LIMIT") == 0);
    assert(lift->done.state == 1);
    assert(lift->done.timeout_ms == 10000U);

    const engine_step_t *hpb =
        find_step(find_lane(&p->phases[2], "highpres_bottom_lane"), "highpres_bottom_on");
    assert(hpb != NULL);
    assert(hpb->action_count == 2U);
    assert(hpb->actions[0].type == ENGINE_ACT_WAIT_TIME);
    assert(hpb->actions[0].ms == 8000U);
    assert(hpb->actions[1].type == ENGINE_ACT_IO_SET);
    assert(strcmp(hpb->actions[1].channel, "WATER_HIGHPRES_BOTTOM") == 0);

    const engine_step_t *tbm =
        find_step(find_lane(&p->phases[4], "top_brush_lane"), "top_brush_medium");
    assert(tbm != NULL);
    assert(tbm->actions[0].value == 2);  /* 整数 DO */

    /* pass5 exit_guard 折叠标量经 YAML→JSON 后仍是单行表达式，验证语义 */
    const engine_phase_t *pass5 = &p->phases[5];
    assert(pass5->exit_guard != NULL);
    {
        eval_ctx_t c1 = { 100.0, false, 0.0, 1 };
        engine_expr_env_t env = { resolver, &c1 };
        bool ok = false;
        assert(engine_expr_eval_bool(pass5->exit_guard, &env, &ok));
        assert(ok);

        eval_ctx_t c2 = { 250.0, true, 200.0, 0 };
        env.ctx = &c2;
        assert(engine_expr_eval_bool(pass5->exit_guard, &env, &ok));
        assert(ok);

        eval_ctx_t c3 = { 100.0, false, 0.0, 0 };
        env.ctx = &c3;
        assert(!engine_expr_eval_bool(pass5->exit_guard, &env, &ok));
        assert(ok);
    }

    engine_program_free(p);
    printf("  PASS\n");
}

static void test_schema_rejects(void)
{
    printf("test_schema_rejects\n");
    char err[160];

    /* 不支持的 control 步骤 */
    static const char *bad_control =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"t\",\"phases\":["
        "{\"id\":\"p\",\"entry_guard\":\"true\",\"exit_guard\":\"true\",\"timeout_ms\":1000,"
        "\"lanes\":[{\"id\":\"l\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"control\",\"active_while\":\"true\",\"output\":\"X\"}"
        "]}]}]}}";
    err[0] = '\0';
    assert(engine_program_load_json_string(bad_control, err, sizeof(err)) == NULL);
    assert(err[0] != '\0');

    /* 错误 schema_version */
    static const char *bad_ver =
        "{\"program\":{\"schema_version\":\"2.0\",\"id\":\"t\",\"phases\":["
        "{\"id\":\"p\",\"entry_guard\":\"true\",\"exit_guard\":\"true\",\"timeout_ms\":1000,"
        "\"lanes\":[{\"id\":\"l\",\"steps\":["
        "{\"id\":\"s\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
        "\"done\":{\"type\":\"actions_complete\"}}"
        "]}]}]}}";
    assert(engine_program_load_json_string(bad_ver, err, sizeof(err)) == NULL);

    /* 缺少 phases */
    static const char *no_phases =
        "{\"program\":{\"schema_version\":\"1.0\",\"id\":\"t\"}}";
    assert(engine_program_load_json_string(no_phases, err, sizeof(err)) == NULL);

    /* 非法 JSON */
    static const char *bad_json = "{ not json ]";
    assert(engine_program_load_json_string(bad_json, err, sizeof(err)) == NULL);

    printf("  PASS\n");
}

int main(void)
{
    printf("=== test_engine_config ===\n");
    test_load_structure();
    test_schema_rejects();
    printf("=== ALL PASSED ===\n");
    return 0;
}
