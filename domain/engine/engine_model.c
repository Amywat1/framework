/**
 * @file    engine_model.c
 * @brief   引擎数据模型的释放与字符串枚举解析
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "domain/engine/engine_model.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 释放
 * ------------------------------------------------------------------------- */
static void free_actions(engine_action_t *actions)
{
    /* 动作内不含动态分配，直接释放数组 */
    free(actions);
}

static void free_step(engine_step_t *step)
{
    if (step->trigger.type == ENGINE_TRIG_CONDITION)
    {
        engine_expr_free(step->trigger.cond);
    }
    engine_expr_free(step->guard);
    free_actions(step->actions);
}

static void free_phase(engine_phase_t *phase)
{
    engine_expr_free(phase->entry_guard);
    engine_expr_free(phase->exit_guard);
    free_actions(phase->on_enter);
    free_actions(phase->on_exit);

    for (unsigned i = 0U; i < phase->lane_count; ++i)
    {
        engine_lane_t *lane = &phase->lanes[i];
        for (unsigned j = 0U; j < lane->step_count; ++j)
        {
            free_step(&lane->steps[j]);
        }
        free(lane->steps);
    }
    free(phase->lanes);
}

void engine_program_free(engine_program_t *prog)
{
    if (prog == NULL)
    {
        return;
    }

    free(prog->params);
    free(prog->axes);
    free(prog->markers);

    for (unsigned i = 0U; i < prog->interlock_count; ++i)
    {
        engine_interlock_t *ilk = &prog->interlocks[i];
        engine_expr_free(ilk->condition);
        engine_expr_free(ilk->reset_condition);
        free_actions(ilk->actions);
    }
    free(prog->interlocks);

    for (unsigned i = 0U; i < prog->phase_count; ++i)
    {
        free_phase(&prog->phases[i]);
    }
    free(prog->phases);

    free(prog);
}

/* -------------------------------------------------------------------------
 * 字符串到枚举
 * ------------------------------------------------------------------------- */
bool engine_direction_from_str(const char *s, engine_direction_t *out)
{
    if ((s == NULL) || (out == NULL)) { return false; }
    if (strcmp(s, "none") == 0)     { *out = ENGINE_DIR_NONE;     return true; }
    if (strcmp(s, "forward") == 0)  { *out = ENGINE_DIR_FORWARD;  return true; }
    if (strcmp(s, "backward") == 0) { *out = ENGINE_DIR_BACKWARD; return true; }
    return false;
}

bool engine_error_strategy_from_str(const char *s, engine_error_strategy_t *out)
{
    if ((s == NULL) || (out == NULL)) { return false; }
    if (strcmp(s, "stop") == 0)       { *out = ENGINE_ERR_STOP;       return true; }
    if (strcmp(s, "halt_phase") == 0) { *out = ENGINE_ERR_HALT_PHASE; return true; }
    if (strcmp(s, "skip") == 0)       { *out = ENGINE_ERR_SKIP;       return true; }
    if (strcmp(s, "degrade") == 0)    { *out = ENGINE_ERR_DEGRADE;    return true; }
    return false;
}

bool engine_interlock_action_from_str(const char *s, engine_interlock_action_t *out)
{
    if ((s == NULL) || (out == NULL)) { return false; }
    if (strcmp(s, "halt_all") == 0)      { *out = ENGINE_ILK_HALT_ALL;   return true; }
    if (strcmp(s, "halt_phase") == 0)    { *out = ENGINE_ILK_HALT_PHASE; return true; }
    if (strcmp(s, "custom_action") == 0) { *out = ENGINE_ILK_CUSTOM;     return true; }
    return false;
}

bool engine_edge_from_str(const char *s, engine_edge_t *out)
{
    if ((s == NULL) || (out == NULL)) { return false; }
    if (strcmp(s, "rising") == 0)  { *out = ENGINE_EDGE_RISING;  return true; }
    if (strcmp(s, "falling") == 0) { *out = ENGINE_EDGE_FALLING; return true; }
    if (strcmp(s, "high") == 0)    { *out = ENGINE_EDGE_HIGH;    return true; }
    if (strcmp(s, "low") == 0)     { *out = ENGINE_EDGE_LOW;     return true; }
    return false;
}

bool engine_trigger_type_from_str(const char *s, engine_trigger_type_t *out)
{
    if ((s == NULL) || (out == NULL)) { return false; }
    if (strcmp(s, "condition") == 0) { *out = ENGINE_TRIG_CONDITION; return true; }
    if (strcmp(s, "signal") == 0)    { *out = ENGINE_TRIG_SIGNAL;    return true; }
    return false;
}

bool engine_done_type_from_str(const char *s, engine_done_type_t *out)
{
    if ((s == NULL) || (out == NULL)) { return false; }
    if (strcmp(s, "actions_complete") == 0) { *out = ENGINE_DONE_ACTIONS_COMPLETE; return true; }
    if (strcmp(s, "trigger_exit") == 0)     { *out = ENGINE_DONE_TRIGGER_EXIT;     return true; }
    if (strcmp(s, "signal") == 0)           { *out = ENGINE_DONE_SIGNAL;           return true; }
    if (strcmp(s, "timeout") == 0)          { *out = ENGINE_DONE_TIMEOUT;          return true; }
    return false;
}
