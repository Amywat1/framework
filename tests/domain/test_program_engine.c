/**
 * @file    test_wash_engine.c
 * @brief   wash engine domain 单元测试
 */

#include "common/sw_error.h"
#include "domain/program_engine/engine/engine.h"
#include "domain/program_engine/engine/engine_expr.h"
#include "domain/program_engine/engine/engine_io.h"
#include "domain/program_engine/engine/engine_profile.h"
#include "domain/program_engine/model/engine_model.h"
#include "domain/program_engine/model/engine_program_validate.h"
#include "unity.h"

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

static int eval_int(const char *text, const expr_var_t *vars)
{
    bool              ok   = false;
    engine_expr_t    *expr = engine_expr_compile(text);
    engine_expr_env_t env  = {expr_resolve, (void *)vars};
    double            value;

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
    engine_expr_env_t env  = {expr_resolve, (void *)vars};
    bool              value;

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
    int         value;
} named_output_t;

typedef struct {
    const char *name;
    double      position;
    double      speed;
    bool        valid;
} named_axis_t;

static named_signal_t s_signals[IO_MAX];
static named_output_t s_outputs[IO_MAX];
static named_axis_t   s_axes[IO_MAX];

static int find_signal(const char *name)
{
    for (int i = 0; i < IO_MAX; ++i) {
        if ((s_signals[i].name != NULL) && (strcmp(s_signals[i].name, name) == 0)) {
            return i;
        }
    }
    return -1;
}

static int find_output(const char *name)
{
    for (int i = 0; i < IO_MAX; ++i) {
        if ((s_outputs[i].name != NULL) && (strcmp(s_outputs[i].name, name) == 0)) {
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

static int io_read_signal(const char *name)
{
    int idx = find_signal(name);
    return (idx >= 0) ? s_signals[idx].value : 0;
}

static sw_err_t io_read_axis(const char *name, double *out_pos, double *out_speed, bool *out_valid)
{
    int idx;

    if ((name == NULL) || (out_pos == NULL) || (out_speed == NULL) || (out_valid == NULL)) {
        return SW_ERR_PARAM;
    }

    idx = find_axis(name);
    if (idx < 0) {
        return SW_ERR_PARAM;
    }

    *out_pos   = s_axes[idx].position;
    *out_speed = s_axes[idx].speed;
    *out_valid = s_axes[idx].valid;
    return SW_OK;
}

static void io_write_output(const char *name, int value)
{
    int idx = find_output(name);
    if (idx >= 0) {
        s_outputs[idx].value = value;
    }
}

static const engine_io_ops_t s_io_ops = {
    .read_signal  = io_read_signal,
    .read_axis    = io_read_axis,
    .write_output = io_write_output,
};

static const char *const         s_catalog_signals[] = {"ESTOP", "EXIT", "GO", "MARK"};
static const char *const         s_catalog_outputs[] = {"AOUT", "BOUT", "CTRL"};
static const char *const         s_catalog_axes[]    = {"gantry"};
static const engine_io_catalog_t s_catalog           = {
              .signals      = s_catalog_signals,
              .signal_count = 4U,
              .outputs      = s_catalog_outputs,
              .output_count = 3U,
              .axes         = s_catalog_axes,
              .axis_count   = 1U,
};

static void io_reset(void)
{
    memset(s_signals, 0, sizeof(s_signals));
    memset(s_outputs, 0, sizeof(s_outputs));
    memset(s_axes, 0, sizeof(s_axes));

    s_signals[0].name = "ESTOP";
    s_signals[1].name = "EXIT";
    s_signals[2].name = "GO";
    s_signals[3].name = "MARK";

    s_outputs[0].name = "AOUT";
    s_outputs[1].name = "BOUT";
    s_outputs[2].name = "CTRL";

    s_axes[0].name     = "gantry";
    s_axes[0].position = 123.0;
    s_axes[0].speed    = 4.0;
    s_axes[0].valid    = true;

    {
        const engine_io_backend_t backend = {
            .ops     = &s_io_ops,
            .catalog = &s_catalog,
        };
        engine_io_register(&backend);
    }
}

static engine_expr_t *compile_ok(const char *text)
{
    engine_expr_t *expr = engine_expr_compile(text);
    TEST_ASSERT_NOT_NULL(expr);
    return expr;
}

static engine_action_t *one_io_action(const char *channel, int value)
{
    engine_action_t *actions = (engine_action_t *)calloc(1U, sizeof(engine_action_t));
    TEST_ASSERT_NOT_NULL(actions);
    actions[0].type = ENGINE_ACT_IO_SET;
    (void)snprintf(actions[0].channel, sizeof(actions[0].channel), "%s", channel);
    actions[0].value = value;
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
    phase->on_exit       = one_io_action("AOUT", 0);
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
    step->actions      = one_io_action("AOUT", 1);
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
    step->actions      = one_io_action("BOUT", 1);
    step->action_count = 1U;
    step->done.type    = ENGINE_DONE_ACTIONS_COMPLETE;

    return prog;
}

void setUp(void)
{
    io_reset();
    engine_profile_register(NULL);
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
    static const engine_profile_provider_t provider = {
        .height_at = profile_height_at,
        .in_zone   = profile_in_zone,
        .ctx       = NULL,
    };
    const expr_var_t vars[] = {
        {"axes.gantry.position", 150.0},
        {NULL,                   0.0  },
    };

    TEST_ASSERT_TRUE(eval_bool_value("body_contains(150, 100, 200)", vars));
    TEST_ASSERT_TRUE(eval_bool_value("body_covers(150, 200, 100)", vars));

    engine_profile_register(&provider);
    TEST_ASSERT_EQUAL_INT(160, eval_int("profile.height_at(axes.gantry.position, 0)", vars));
    TEST_ASSERT_TRUE(eval_bool_value("profile.in_zone(\"mirror\", axes.gantry.position, false)", vars));
    TEST_ASSERT_FALSE(eval_bool_value("profile.in_zone('roof', axes.gantry.position, false)", vars));

    engine_profile_register(NULL);
    TEST_ASSERT_EQUAL_INT(77, eval_int("profile.height_at(axes.gantry.position, 77)", vars));
    TEST_ASSERT_TRUE(eval_bool_value("profile.in_zone(\"missing\", axes.gantry.position, true)", vars));
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

    TEST_ASSERT_EQUAL_INT(SW_OK, engine_program_validate(prog, &s_catalog, err, sizeof(err)));

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

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, engine_program_validate(prog, &s_catalog, err, sizeof(err)));
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(err));

    engine_program_free(prog);
}

static void test_engine_runtime_runs_steps_and_finishes_phase(void)
{
    engine_t         *engine = engine_create();
    engine_program_t *prog   = make_program();

    TEST_ASSERT_NOT_NULL(engine);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_load_program(engine, prog));
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_start(engine));
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_RUNNING, engine_state(engine));
    TEST_ASSERT_EQUAL_INT(0, engine_current_phase(engine));
    TEST_ASSERT_EQUAL_STRING("p0", engine_current_phase_id(engine));
    TEST_ASSERT_EQUAL_INT(ENGINE_DIR_FORWARD, engine_current_direction(engine));

    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(0, s_outputs[0].value);

    engine_tick(engine, 100U);
    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(1, s_outputs[0].value);
    TEST_ASSERT_EQUAL_INT(0, s_outputs[1].value);

    s_signals[2].value = 1;
    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(1, s_outputs[1].value);

    s_signals[1].value = 1;
    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_DONE, engine_state(engine));
    TEST_ASSERT_EQUAL_INT(0, s_outputs[0].value);

    engine_destroy(engine);
}

static void test_engine_runtime_halt_all_interlock_clears_outputs(void)
{
    engine_t         *engine = engine_create();
    engine_program_t *prog   = make_program();

    TEST_ASSERT_NOT_NULL(engine);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_load_program(engine, prog));
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_start(engine));

    engine_tick(engine, 300U);
    engine_tick(engine, 1U);
    TEST_ASSERT_EQUAL_INT(1, s_outputs[0].value);

    s_signals[0].value = 1;
    engine_tick(engine, 100U);
    TEST_ASSERT_EQUAL_INT(ENGINE_STATE_HALTED, engine_state(engine));
    TEST_ASSERT_EQUAL_INT(0, s_outputs[0].value);

    engine_destroy(engine);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_engine_expr_evaluates_arithmetic_logic_and_vars);
    RUN_TEST(test_engine_expr_evaluates_profile_functions);
    RUN_TEST(test_engine_model_parse_clone_and_validate);
    RUN_TEST(test_engine_validate_rejects_unknown_after);
    RUN_TEST(test_engine_runtime_runs_steps_and_finishes_phase);
    RUN_TEST(test_engine_runtime_halt_all_interlock_clears_outputs);

    return UNITY_END();
}
