/**
 * @file    test_program_engine.c
 * @brief   wash engine domain 单元测试
 */

#include "adapters/outbound/hal/sim/engine_actuator_sim.h"
#include "common/sw_error.h"
#include "domain/ports/outbound/program_engine/engine_environment_provider.h"
#include "domain/program_engine/engine/engine.h"
#include "domain/program_engine/engine/engine_expr.h"
#include "domain/program_engine/model/engine_model.h"
#include "domain/program_engine/model/engine_program_validate.h"
#include "wdf_test_spec.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    double      value;
} expr_var_t;

static bool expr_resolve(void *ctx, const char *name, double *out_value)
{
    const expr_var_t *vars = (const expr_var_t *)ctx;

    for (unsigned i = 0U; vars[i].name != NULL; ++i) {
        if (strcmp(vars[i].name, name) == 0) {
            *out_value = vars[i].value;
            return true;
        }
    }
    return false;
}

static bool profile_height_at(void *ctx, double pos, double default_value, double *out_height)
{
    (void)ctx;
    (void)default_value;

    if (out_height == NULL) {
        return false;
    }
    *out_height = pos + 10.0;
    return true;
}

static bool profile_in_zone(void *ctx, const char *zone, double pos, bool default_value, bool *out_in_zone)
{
    (void)ctx;
    (void)default_value;

    if ((zone == NULL) || (out_in_zone == NULL)) {
        return false;
    }
    *out_in_zone = (strcmp(zone, "mirror") == 0) && (pos >= 100.0) && (pos <= 200.0);
    return true;
}

static bool s_profile_enabled;

static int eval_int(const char *text, const expr_var_t *vars)
{
    bool              ok   = false;
    engine_expr_t    *expr = engine_expr_compile(text);
    engine_expr_env_t env  = {
         .resolve           = expr_resolve,
         .profile_height_at = s_profile_enabled ? profile_height_at : NULL,
         .profile_in_zone   = s_profile_enabled ? profile_in_zone : NULL,
         .ctx               = (void *)vars,
    };
    double value;

    TEST_ASSERT_NOT_NULL(expr);
    value = engine_expr_eval(expr, &env, &ok);
    TEST_ASSERT_TRUE(ok);
    engine_expr_free(expr);
    return (int)value;
}

static bool eval_bool_value(const char *text, const expr_var_t *vars)
{
    bool              ok   = false;
    engine_expr_t    *expr = engine_expr_compile(text);
    engine_expr_env_t env  = {
         .resolve           = expr_resolve,
         .profile_height_at = s_profile_enabled ? profile_height_at : NULL,
         .profile_in_zone   = s_profile_enabled ? profile_in_zone : NULL,
         .ctx               = (void *)vars,
    };
    bool value;

    TEST_ASSERT_NOT_NULL(expr);
    value = engine_expr_eval_bool(expr, &env, &ok);
    TEST_ASSERT_TRUE(ok);
    engine_expr_free(expr);
    return value;
}

#define IO_MAX 16

typedef struct {
    const char *name;
    int         value;
} named_signal_t;

typedef struct {
    const char *name;
    double      position;
    double      speed;
    bool        valid;
} named_axis_t;

static named_signal_t       s_signals[IO_MAX];
static named_axis_t         s_axes[IO_MAX];
static engine_io_t          s_io;
static engine_environment_t s_environment;

static int find_signal(const char *name)
{
    for (int i = 0; i < IO_MAX; ++i) {
        if ((s_signals[i].name != NULL) && (strcmp(s_signals[i].name, name) == 0)) {
            return i;
        }
    }
    return -1;
}

static int find_axis(const char *name)
{
    for (int i = 0; i < IO_MAX; ++i) {
        if ((s_axes[i].name != NULL) && (strcmp(s_axes[i].name, name) == 0)) {
            return i;
        }
    }
    return -1;
}

static sw_err_t io_read_signal(void *ctx, unsigned signal_id, int *out_value)
{
    const engine_io_catalog_t *catalog = (const engine_io_catalog_t *)ctx;
    int                        index;

    if ((out_value == NULL) || (signal_id >= catalog->signal_count)) {
        return SW_ERR_PARAM;
    }
    index = find_signal(catalog->signals[signal_id]);
    if (index < 0) {
        return SW_ERR_NOT_FOUND;
    }
    *out_value = s_signals[index].value;
    return SW_OK;
}

static sw_err_t io_read_axis(void *ctx, unsigned axis_id, engine_axis_sample_t *out_sample)
{
    const engine_io_catalog_t *catalog = (const engine_io_catalog_t *)ctx;
    int                        idx;

    if ((out_sample == NULL) || (axis_id >= catalog->axis_count)) {
        return SW_ERR_PARAM;
    }

    idx = find_axis(catalog->axes[axis_id]);
    if (idx < 0) {
        return SW_ERR_PARAM;
    }

    out_sample->position = s_axes[idx].position;
    out_sample->speed    = s_axes[idx].speed;
    out_sample->valid    = s_axes[idx].valid;
    return SW_OK;
}

static const engine_io_ops_t s_io_ops = {
    .read_signal = io_read_signal,
    .read_axis   = io_read_axis,
};

static const char *const         s_catalog_signals[] = {"ESTOP", "EXIT", "GO", "MARK"};
static const char *const         s_catalog_axes[]    = {"gantry"};
static const engine_io_catalog_t s_io_catalog        = {
           .signals      = s_catalog_signals,
           .signal_count = 4U,
           .axes         = s_catalog_axes,
           .axis_count   = 1U,
};

static void io_reset(void)
{
    memset(s_signals, 0, sizeof(s_signals));
    memset(s_axes, 0, sizeof(s_axes));

    s_signals[0].name = "ESTOP";
    s_signals[1].name = "EXIT";
    s_signals[2].name = "GO";
    s_signals[3].name = "MARK";

    s_axes[0].name     = "gantry";
    s_axes[0].position = 123.0;
    s_axes[0].speed    = 4.0;
    s_axes[0].valid    = true;

    TEST_ASSERT_EQUAL_INT(SW_OK, engine_io_provider_bind(&s_io, &s_io_ops, (void *)&s_io_catalog, &s_io_catalog));
    s_environment.io       = &s_io;
    s_environment.actuator = engine_actuator_sim_instance();
}

static engine_expr_t *compile_ok(const char *text)
{
    engine_expr_t *expr = engine_expr_compile(text);
    TEST_ASSERT_NOT_NULL(expr);
    return expr;
}

static engine_action_t *one_act(const char *resource, const char *cmd, int gear)
{
    engine_action_t *actions = (engine_action_t *)calloc(1U, sizeof(engine_action_t));

    TEST_ASSERT_NOT_NULL(actions);
    actions[0].type = ENGINE_ACT_INTENT;
    (void)snprintf(actions[0].intent.resource, sizeof(actions[0].intent.resource), "%s", resource);
    (void)snprintf(actions[0].intent.cmd, sizeof(actions[0].intent.cmd), "%s", cmd);
    actions[0].intent.gear = gear;
    return actions;
}

static engine_program_t *make_program(void)
{
    engine_program_t   *prog = (engine_program_t *)calloc(1U, sizeof(engine_program_t));
    engine_interlock_t *interlock;
    engine_phase_t     *phase;
    engine_lane_t      *lane;
    engine_step_t      *step;

    TEST_ASSERT_NOT_NULL(prog);
    (void)snprintf(prog->schema_version, sizeof(prog->schema_version), "%s", "1.0");
    (void)snprintf(prog->id, sizeof(prog->id), "%s", "unit");
    (void)snprintf(prog->name, sizeof(prog->name), "%s", "unit");

    prog->interlock_count = 1U;
    prog->interlocks      = (engine_interlock_t *)calloc(1U, sizeof(engine_interlock_t));
    TEST_ASSERT_NOT_NULL(prog->interlocks);
    interlock = &prog->interlocks[0];
    (void)snprintf(interlock->id, sizeof(interlock->id), "%s", "estop");
    interlock->condition       = compile_ok("ESTOP == 1");
    interlock->reset_condition = compile_ok("ESTOP == 0");
    interlock->action          = ENGINE_ILK_HALT_ALL;
    interlock->priority        = 0;

    prog->phase_count = 1U;
    prog->phases      = (engine_phase_t *)calloc(1U, sizeof(engine_phase_t));
    TEST_ASSERT_NOT_NULL(prog->phases);
    phase = &prog->phases[0];
    (void)snprintf(phase->id, sizeof(phase->id), "%s", "p0");
    (void)snprintf(phase->name, sizeof(phase->name), "%s", "phase");
    phase->direction     = ENGINE_DIR_FORWARD;
    phase->entry_guard   = compile_ok("true");
    phase->exit_guard    = compile_ok("EXIT == 1");
    phase->timeout_ms    = 10000U;
    phase->on_timeout    = ENGINE_ERR_STOP;
    phase->on_exit       = one_act("aout", "stop", 0);
    phase->on_exit_count = 1U;
    phase->lane_count    = 1U;
    phase->lanes         = (engine_lane_t *)calloc(1U, sizeof(engine_lane_t));
    TEST_ASSERT_NOT_NULL(phase->lanes);

    lane = &phase->lanes[0];
    (void)snprintf(lane->id, sizeof(lane->id), "%s", "lane");
    lane->step_count = 2U;
    lane->steps      = (engine_step_t *)calloc(2U, sizeof(engine_step_t));
    TEST_ASSERT_NOT_NULL(lane->steps);

    step = &lane->steps[0];
    (void)snprintf(step->id, sizeof(step->id), "%s", "start");
    step->type         = ENGINE_STEP_EVENT;
    step->on_error     = ENGINE_ERR_STOP;
    step->trigger.type = ENGINE_TRIG_CONDITION;
    step->trigger.cond = compile_ok("phase.elapsed_ms >= 200");
    step->actions      = one_act("aout", "run", 1);
    step->action_count = 1U;
    step->done.type    = ENGINE_DONE_ACTIONS_COMPLETE;

    step = &lane->steps[1];
    (void)snprintf(step->id, sizeof(step->id), "%s", "after_start");
    step->type         = ENGINE_STEP_EVENT;
    step->on_error     = ENGINE_ERR_STOP;
    step->trigger.type = ENGINE_TRIG_SIGNAL;
    (void)snprintf(step->trigger.signal, sizeof(step->trigger.signal), "%s", "GO");
    step->trigger.edge = ENGINE_EDGE_RISING;
    (void)snprintf(step->after[0], sizeof(step->after[0]), "%s", "start");
    step->after_count  = 1U;
    step->actions      = one_act("bout", "run", 1);
    step->action_count = 1U;
    step->done.type    = ENGINE_DONE_ACTIONS_COMPLETE;

    return prog;
}

void setUp(void)
{
    engine_actuator_sim_reset();
    io_reset();
    s_profile_enabled = false;
}

void tearDown(void)
{
}

static void test_engine_expr_evaluates_arithmetic_logic_and_vars(void)
{
    const expr_var_t vars[] = {
        {"$offset",              20.0 },
        {"axes.gantry.position", 130.0},
        {"MARK",                 1.0  },
        {NULL,                   0.0  },
    };

    TEST_ASSERT_EQUAL_INT(14, eval_int("2 + 3 * 4", vars));
    TEST_ASSERT_TRUE(eval_bool_value("MARK AND axes.gantry.position >= 100 + $offset", vars));
    TEST_ASSERT_FALSE(eval_bool_value("NOT MARK", vars));
    TEST_ASSERT_NULL(engine_expr_compile("1 +"));
}

static void test_engine_expr_evaluates_profile_functions(void)
{
    const expr_var_t vars[] = {
        {"axes.gantry.position", 150.0},
        {NULL,                   0.0  },
    };

    TEST_ASSERT_TRUE(eval_bool_value("body_contains(150, 100, 200)", vars));
    TEST_ASSERT_TRUE(eval_bool_value("body_covers(150, 200, 100)", vars));

    s_profile_enabled = true;
    TEST_ASSERT_EQUAL_INT(160, eval_int("profile.height_at(axes.gantry.position, 0)", vars));
    TEST_ASSERT_TRUE(eval_bool_value("profile.in_zone(\"mirror\", axes.gantry.position, false)", vars));
    TEST_ASSERT_FALSE(eval_bool_value("profile.in_zone('roof', axes.gantry.position, false)", vars));

    s_profile_enabled = false;
    TEST_ASSERT_EQUAL_INT(77, eval_int("profile.height_at(axes.gantry.position, 77)", vars));
    TEST_ASSERT_TRUE(eval_bool_value("profile.in_zone(\"missing\", axes.gantry.position, true)", vars));
}

/* -------------------------------------------------------------------------
 * 加载期函数校验
 *
 * 这些错误原先只在求值期表现为 *ok = false，且拿不到函数名——方案里写错一个
 * 函数名要洗到那一步才失败。方案是部署时下发的资产，加载期拒绝代价低得多。
 * ------------------------------------------------------------------------- */
static void test_engine_expr_rejects_unknown_function_at_compile(void)
{
    TEST_ASSERT_NULL(engine_expr_compile("body_containz(150, 100, 200)"));
    TEST_ASSERT_NOT_NULL(strstr(engine_expr_last_error(), "未知函数"));
    /* 错误里必须点名函数，否则排查还得回去翻方案 */
    TEST_ASSERT_NOT_NULL(strstr(engine_expr_last_error(), "body_containz"));
}

static void test_engine_expr_rejects_wrong_arity_at_compile(void)
{
    TEST_ASSERT_NULL(engine_expr_compile("body_contains(150, 100)"));
    TEST_ASSERT_NOT_NULL(strstr(engine_expr_last_error(), "body_contains"));
    TEST_ASSERT_NOT_NULL(strstr(engine_expr_last_error(), "3"));

    TEST_ASSERT_NULL(engine_expr_compile("profile.height_at(1, 2, 3)"));
    TEST_ASSERT_NOT_NULL(strstr(engine_expr_last_error(), "profile.height_at"));
}

/* profile.in_zone 首参是区域名，必须为字符串字面量：字符串在求值期一律
 * 置 *ok = false，故这类位置只能在加载期查出来 */
static void test_engine_expr_rejects_non_literal_zone_name(void)
{
    TEST_ASSERT_NULL(engine_expr_compile("profile.in_zone(axes.gantry.position, 1, 0)"));
    TEST_ASSERT_NOT_NULL(strstr(engine_expr_last_error(), "字符串字面量"));
}

/* 嵌套位置同样要查：函数出现在参数里、三目分支里都不能漏 */
static void test_engine_expr_checks_nested_functions(void)
{
    TEST_ASSERT_NULL(engine_expr_compile("body_contains(profile.bogus(1), 100, 200)"));
    TEST_ASSERT_NOT_NULL(strstr(engine_expr_last_error(), "profile.bogus"));

    TEST_ASSERT_NULL(engine_expr_compile("1 > 0 ? body_contains(1, 2, 3) : nope(1)"));
    TEST_ASSERT_NOT_NULL(strstr(engine_expr_last_error(), "nope"));

    TEST_ASSERT_NULL(engine_expr_compile("NOT bad_fn(1)"));
    TEST_ASSERT_NOT_NULL(strstr(engine_expr_last_error(), "bad_fn"));
}

/* 白名单里的每个函数都必须真能求值，防白名单与求值分派漂移：
 * 表里加了名字而 eval_node 漏加分支时，加载期放过、求值期落到兜底的
 * *ok = false。故这里逐个实际求值——eval_int / eval_bool_value 内部断言
 * ok 为真，只判"能编译"是抓不到漏分支的。
 *
 * 新增内建函数时同步在此加一行，否则漂移不会被拦住。 */
static void test_engine_expr_whitelist_all_evaluable(void)
{
    const expr_var_t vars[] = {
        {NULL, 0.0},
    };

    TEST_ASSERT_TRUE(eval_bool_value("body_contains(150, 100, 200)", vars));
    TEST_ASSERT_TRUE(eval_bool_value("body_covers(150, 200, 100)", vars));

    /* provider 未注册时走默认值分支，同样要求 ok 为真 */
    s_profile_enabled = false;
    TEST_ASSERT_EQUAL_INT(42, eval_int("profile.height_at(1, 42)", vars));
    TEST_ASSERT_TRUE(eval_bool_value("profile.in_zone(\"z\", 1, 1)", vars));
}

static void test_engine_model_parse_clone_and_validate(void)
{
    char                    err[160];
    engine_direction_t      dir;
    engine_error_strategy_t strategy;
    engine_program_t       *prog = make_program();
    engine_program_t       *copy;

    TEST_ASSERT_TRUE(engine_direction_from_str("forward", &dir));
    TEST_ASSERT_EQUAL_INT(ENGINE_DIR_FORWARD, dir);
    TEST_ASSERT_TRUE(engine_error_strategy_from_str("halt_phase", &strategy));
    TEST_ASSERT_EQUAL_INT(ENGINE_ERR_HALT_PHASE, strategy);
    TEST_ASSERT_FALSE(engine_direction_from_str("sideways", &dir));

    TEST_ASSERT_EQUAL_INT(
        SW_OK,
        engine_program_validate(
            prog, &s_io_catalog, engine_actuator_catalog(s_environment.actuator), NULL, err, sizeof(err)));

    copy = engine_program_clone(prog);
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(prog->id, copy->id);
    TEST_ASSERT_EQUAL_UINT(prog->phase_count, copy->phase_count);
    TEST_ASSERT_NOT_EQUAL((uintptr_t)prog->phases[0].entry_guard, (uintptr_t)copy->phases[0].entry_guard);

    engine_program_free(copy);
    engine_program_free(prog);
}

static void test_engine_validate_rejects_unknown_after(void)
{
    char              err[160];
    engine_program_t *prog = make_program();

    (void)snprintf(prog->phases[0].lanes[0].steps[1].after[0],
                   sizeof(prog->phases[0].lanes[0].steps[1].after[0]),
                   "%s",
                   "missing");

    TEST_ASSERT_EQUAL_INT(
        SW_ERR_PARAM,
        engine_program_validate(
            prog, &s_io_catalog, engine_actuator_catalog(s_environment.actuator), NULL, err, sizeof(err)));
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(err));

    engine_program_free(prog);
}

static void test_engine_runtime_runs_steps_and_finishes_phase(void)
{
    engine_t         *engine = engine_create(&s_environment);
    engine_program_t *prog   = make_program();

    TEST_ASSERT_NOT_NULL(engine);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_load_program(engine, prog));
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_start(engine));
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_RUNNING, engine_state(engine));
    TEST_ASSERT_EQUAL_INT(0, engine_current_phase(engine));
    TEST_ASSERT_EQUAL_STRING("p0", engine_current_phase_id(engine));
    TEST_ASSERT_EQUAL_INT(ENGINE_DIR_FORWARD, engine_current_direction(engine));

    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(0, engine_actuator_sim_active("aout"));

    engine_tick(engine, 100U);
    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("aout"));
    TEST_ASSERT_EQUAL_INT(0, engine_actuator_sim_active("bout"));

    s_signals[2].value = 1;
    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("bout"));

    s_signals[1].value = 1;
    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_DONE, engine_state(engine));
    TEST_ASSERT_EQUAL_INT(0, engine_actuator_sim_active("aout"));

    engine_destroy(engine);
}

static void test_engine_runtime_auto_releases_held_except_keep(void)
{
    engine_t         *engine;
    engine_program_t *prog;
    engine_phase_t   *p0;
    engine_phase_t   *p1;
    engine_lane_t    *lane;
    engine_step_t    *step;

    prog = (engine_program_t *)calloc(1U, sizeof(engine_program_t));
    TEST_ASSERT_NOT_NULL(prog);
    (void)snprintf(prog->schema_version, sizeof(prog->schema_version), "%s", "1.0");
    (void)snprintf(prog->id, sizeof(prog->id), "%s", "keep_ut");
    (void)snprintf(prog->name, sizeof(prog->name), "%s", "keep_ut");

    prog->interlock_count = 1U;
    prog->interlocks      = (engine_interlock_t *)calloc(1U, sizeof(engine_interlock_t));
    TEST_ASSERT_NOT_NULL(prog->interlocks);
    (void)snprintf(prog->interlocks[0].id, sizeof(prog->interlocks[0].id), "%s", "estop");
    prog->interlocks[0].condition       = compile_ok("ESTOP == 1");
    prog->interlocks[0].reset_condition = compile_ok("ESTOP == 0");
    prog->interlocks[0].action          = ENGINE_ILK_HALT_ALL;

    prog->phase_count = 2U;
    prog->phases      = (engine_phase_t *)calloc(2U, sizeof(engine_phase_t));
    TEST_ASSERT_NOT_NULL(prog->phases);

    p0 = &prog->phases[0];
    (void)snprintf(p0->id, sizeof(p0->id), "%s", "p0");
    p0->entry_guard = compile_ok("true");
    p0->exit_guard  = compile_ok("phase.elapsed_ms >= 100");
    p0->timeout_ms  = 5000U;
    p0->keep_count  = 1U;
    p0->keep        = (char (*)[ENGINE_NAME_MAX])calloc(1U, sizeof(*p0->keep));
    TEST_ASSERT_NOT_NULL(p0->keep);
    (void)snprintf(p0->keep[0], ENGINE_NAME_MAX, "%s", "aout");
    p0->lane_count = 1U;
    p0->lanes      = (engine_lane_t *)calloc(1U, sizeof(engine_lane_t));
    TEST_ASSERT_NOT_NULL(p0->lanes);
    lane             = &p0->lanes[0];
    lane->step_count = 1U;
    lane->steps      = (engine_step_t *)calloc(1U, sizeof(engine_step_t));
    TEST_ASSERT_NOT_NULL(lane->steps);
    step = &lane->steps[0];
    (void)snprintf(step->id, sizeof(step->id), "%s", "start");
    step->type         = ENGINE_STEP_EVENT;
    step->on_error     = ENGINE_ERR_STOP;
    step->trigger.type = ENGINE_TRIG_CONDITION;
    step->trigger.cond = compile_ok("true");
    {
        engine_action_t *acts = (engine_action_t *)calloc(2U, sizeof(engine_action_t));
        TEST_ASSERT_NOT_NULL(acts);
        acts[0].type = ENGINE_ACT_INTENT;
        (void)snprintf(acts[0].intent.resource, sizeof(acts[0].intent.resource), "%s", "aout");
        (void)snprintf(acts[0].intent.cmd, sizeof(acts[0].intent.cmd), "%s", "run");
        acts[0].intent.gear = 1;
        acts[1].type        = ENGINE_ACT_INTENT;
        (void)snprintf(acts[1].intent.resource, sizeof(acts[1].intent.resource), "%s", "bout");
        (void)snprintf(acts[1].intent.cmd, sizeof(acts[1].intent.cmd), "%s", "run");
        acts[1].intent.gear = 1;
        step->actions       = acts;
        step->action_count  = 2U;
    }
    step->done.type = ENGINE_DONE_ACTIONS_COMPLETE;

    p1 = &prog->phases[1];
    (void)snprintf(p1->id, sizeof(p1->id), "%s", "p1");
    p1->entry_guard = compile_ok("true");
    p1->exit_guard  = compile_ok("EXIT == 1");
    p1->timeout_ms  = 5000U;
    p1->lane_count  = 1U;
    p1->lanes       = (engine_lane_t *)calloc(1U, sizeof(engine_lane_t));
    TEST_ASSERT_NOT_NULL(p1->lanes);
    p1->lanes[0].step_count = 1U;
    p1->lanes[0].steps      = (engine_step_t *)calloc(1U, sizeof(engine_step_t));
    TEST_ASSERT_NOT_NULL(p1->lanes[0].steps);
    step = &p1->lanes[0].steps[0];
    (void)snprintf(step->id, sizeof(step->id), "%s", "idle");
    step->type         = ENGINE_STEP_EVENT;
    step->on_error     = ENGINE_ERR_STOP;
    step->trigger.type = ENGINE_TRIG_CONDITION;
    step->trigger.cond = compile_ok("false");
    step->done.type    = ENGINE_DONE_ACTIONS_COMPLETE;

    engine = engine_create(&s_environment);
    TEST_ASSERT_NOT_NULL(engine);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_load_program(engine, prog));
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_start(engine));

    engine_tick(engine, 50U);
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("aout"));
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("bout"));
    TEST_ASSERT_EQUAL_INT(0, engine_current_phase(engine));

    engine_tick(engine, 60U);
    TEST_ASSERT_EQUAL_INT(1, engine_current_phase(engine));
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("aout"));
    TEST_ASSERT_EQUAL_INT(0, engine_actuator_sim_active("bout"));

    s_signals[1].value = 1;
    engine_tick(engine, 10U);
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_DONE, engine_state(engine));
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("aout"));

    engine_destroy(engine);
}

static void test_engine_runtime_halt_all_interlock_clears_outputs(void)
{
    engine_t         *engine = engine_create(&s_environment);
    engine_program_t *prog   = make_program();

    TEST_ASSERT_NOT_NULL(engine);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_load_program(engine, prog));
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_start(engine));

    engine_tick(engine, 300U);
    engine_tick(engine, 1U);
    TEST_ASSERT_EQUAL_INT(1, engine_actuator_sim_active("aout"));

    s_signals[0].value = 1;
    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_HALTED, engine_state(engine));
    TEST_ASSERT_EQUAL_INT(0, engine_actuator_sim_active("aout"));

    engine_destroy(engine);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_engine_expr_evaluates_arithmetic_logic_and_vars, "", "验证引擎表达式计算算术、逻辑和变量");
    WDF_RUN_TEST(test_engine_expr_evaluates_profile_functions, "", "验证引擎表达式计算配置函数");
    WDF_RUN_TEST(test_engine_expr_rejects_unknown_function_at_compile, "", "验证表达式在编译期拒绝未知函数");
    WDF_RUN_TEST(test_engine_expr_rejects_wrong_arity_at_compile, "", "验证表达式在编译期拒绝错误参数数量");
    WDF_RUN_TEST(test_engine_expr_rejects_non_literal_zone_name, "", "验证表达式拒绝非字面量区域名称");
    WDF_RUN_TEST(test_engine_expr_checks_nested_functions, "", "验证程序引擎表达式检查嵌套函数");
    WDF_RUN_TEST(test_engine_expr_whitelist_all_evaluable, "", "验证程序引擎表达式白名单全部可求值");
    WDF_RUN_TEST(test_engine_model_parse_clone_and_validate, "", "验证程序引擎模型解析克隆并校验");
    WDF_RUN_TEST(test_engine_validate_rejects_unknown_after, "", "验证引擎校验拒绝未知后继步骤");
    WDF_RUN_TEST(test_engine_runtime_runs_steps_and_finishes_phase, "", "验证程序引擎运行时运行步骤并完成阶段");
    WDF_RUN_TEST(test_engine_runtime_auto_releases_held_except_keep, "", "验证引擎自动释放除保留项外的占用资源");
    WDF_RUN_TEST(test_engine_runtime_halt_all_interlock_clears_outputs, "", "验证程序引擎运行时停止全部联锁清除输出");

    return UNITY_END();
}
