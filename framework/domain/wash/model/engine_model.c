/**
 * @file    engine_model.c
 * @brief   引擎数据模型的释放与字符串枚举解析
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "framework/domain/wash/model/engine_model.h"

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
    if (step->type == ENGINE_STEP_CONTROL)
    {
        engine_expr_free(step->active_while);
        engine_expr_free(step->value_expr);
        return;
    }

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
 * 克隆
 * ------------------------------------------------------------------------- */
static engine_action_t *clone_actions(const engine_action_t *src, unsigned count)
{
    if ((src == NULL) || (count == 0U))
    {
        return NULL;
    }

    engine_action_t *dst = (engine_action_t *)calloc(count, sizeof(engine_action_t));
    if (dst != NULL)
    {
        (void)memcpy(dst, src, count * sizeof(engine_action_t));
    }
    return dst;
}

static bool clone_step(engine_step_t *dst, const engine_step_t *src)
{
    (void)memset(dst, 0, sizeof(engine_step_t));
    (void)memcpy(dst->id, src->id, sizeof(dst->id));
    dst->type      = src->type;
    dst->on_error  = src->on_error;

    if (src->type == ENGINE_STEP_CONTROL)
    {
        (void)memcpy(dst->output, src->output, sizeof(dst->output));
        dst->active_while = engine_expr_clone(src->active_while);
        if ((src->active_while != NULL) && (dst->active_while == NULL))
        {
            return false;
        }
        dst->value_expr = engine_expr_clone(src->value_expr);
        if ((src->value_expr != NULL) && (dst->value_expr == NULL))
        {
            return false;
        }
        return true;
    }

    dst->trigger.type = src->trigger.type;
    dst->trigger.edge = src->trigger.edge;
    (void)memcpy(dst->trigger.signal, src->trigger.signal, sizeof(dst->trigger.signal));
    dst->done         = src->done;
    dst->on_error     = src->on_error;
    dst->after_count  = src->after_count;
    (void)memcpy(dst->after, src->after, sizeof(dst->after));

    if (src->trigger.type == ENGINE_TRIG_CONDITION)
    {
        dst->trigger.cond = engine_expr_clone(src->trigger.cond);
        if ((src->trigger.cond != NULL) && (dst->trigger.cond == NULL))
        {
            return false;
        }
    }

    dst->guard = engine_expr_clone(src->guard);
    if ((src->guard != NULL) && (dst->guard == NULL))
    {
        return false;
    }

    dst->actions = clone_actions(src->actions, src->action_count);
    if ((src->action_count > 0U) && (dst->actions == NULL))
    {
        return false;
    }
    return true;
}

static bool clone_phase(engine_phase_t *dst, const engine_phase_t *src)
{
    (void)memset(dst, 0, sizeof(engine_phase_t));
    (void)memcpy(dst->id, src->id, sizeof(dst->id));
    (void)memcpy(dst->name, src->name, sizeof(dst->name));
    dst->direction  = src->direction;
    dst->timeout_ms = src->timeout_ms;
    dst->on_timeout = src->on_timeout;
    dst->lane_count = src->lane_count;

    dst->entry_guard = engine_expr_clone(src->entry_guard);
    if ((src->entry_guard != NULL) && (dst->entry_guard == NULL))
    {
        return false;
    }

    dst->exit_guard = engine_expr_clone(src->exit_guard);
    if ((src->exit_guard != NULL) && (dst->exit_guard == NULL))
    {
        return false;
    }

    dst->on_enter = clone_actions(src->on_enter, src->on_enter_count);
    if ((src->on_enter_count > 0U) && (dst->on_enter == NULL))
    {
        return false;
    }

    dst->on_exit = clone_actions(src->on_exit, src->on_exit_count);
    if ((src->on_exit_count > 0U) && (dst->on_exit == NULL))
    {
        return false;
    }

    if (src->lane_count > 0U)
    {
        dst->lanes = (engine_lane_t *)calloc(src->lane_count, sizeof(engine_lane_t));
        if (dst->lanes == NULL)
        {
            return false;
        }

        for (unsigned l = 0U; l < src->lane_count; ++l)
        {
            const engine_lane_t *sl = &src->lanes[l];
            engine_lane_t       *dl = &dst->lanes[l];

            (void)memcpy(dl, sl, sizeof(engine_lane_t));
            dl->steps = NULL;
            dl->step_count = sl->step_count;

            if (sl->step_count > 0U)
            {
                dl->steps = (engine_step_t *)calloc(sl->step_count, sizeof(engine_step_t));
                if (dl->steps == NULL)
                {
                    return false;
                }
                for (unsigned s = 0U; s < sl->step_count; ++s)
                {
                    if (!clone_step(&dl->steps[s], &sl->steps[s]))
                    {
                        for (unsigned k = 0U; k < s; ++k)
                        {
                            free_step(&dl->steps[k]);
                        }
                        free(dl->steps);
                        dl->steps     = NULL;
                        dl->step_count = 0U;
                        return false;
                    }
                }
            }
        }
    }
    else
    {
        dst->lanes = NULL;
    }

    return true;
}

engine_program_t *engine_program_clone(const engine_program_t *src)
{
    if (src == NULL)
    {
        return NULL;
    }

    engine_program_t *dst = (engine_program_t *)calloc(1U, sizeof(engine_program_t));
    if (dst == NULL)
    {
        return NULL;
    }

    (void)memcpy(dst, src, sizeof(engine_program_t));
    dst->params     = NULL;
    dst->axes       = NULL;
    dst->markers    = NULL;
    dst->interlocks = NULL;
    dst->phases     = NULL;

    if (src->param_count > 0U)
    {
        dst->params = (engine_param_t *)calloc(src->param_count, sizeof(engine_param_t));
        if (dst->params == NULL)
        {
            goto fail;
        }
        (void)memcpy(dst->params, src->params, src->param_count * sizeof(engine_param_t));
    }

    if (src->axis_count > 0U)
    {
        dst->axes = (engine_axis_t *)calloc(src->axis_count, sizeof(engine_axis_t));
        if (dst->axes == NULL)
        {
            goto fail;
        }
        (void)memcpy(dst->axes, src->axes, src->axis_count * sizeof(engine_axis_t));
    }

    if (src->marker_count > 0U)
    {
        dst->markers = (engine_marker_t *)calloc(src->marker_count, sizeof(engine_marker_t));
        if (dst->markers == NULL)
        {
            goto fail;
        }
        (void)memcpy(dst->markers, src->markers, src->marker_count * sizeof(engine_marker_t));
    }

    if (src->interlock_count > 0U)
    {
        dst->interlocks = (engine_interlock_t *)calloc(src->interlock_count,
                                                       sizeof(engine_interlock_t));
        if (dst->interlocks == NULL)
        {
            goto fail;
        }

        for (unsigned i = 0U; i < src->interlock_count; ++i)
        {
            const engine_interlock_t *si = &src->interlocks[i];
            engine_interlock_t       *di = &dst->interlocks[i];

            (void)memset(di, 0, sizeof(engine_interlock_t));
            (void)memcpy(di->id, si->id, sizeof(di->id));
            di->action    = si->action;
            di->priority  = si->priority;
            di->auto_reset = si->auto_reset;

            di->condition = engine_expr_clone(si->condition);
            if ((si->condition != NULL) && (di->condition == NULL))
            {
                goto fail;
            }
            di->reset_condition = engine_expr_clone(si->reset_condition);
            if ((si->reset_condition != NULL) && (di->reset_condition == NULL))
            {
                goto fail;
            }
            di->actions = clone_actions(si->actions, si->action_count);
            if ((si->action_count > 0U) && (di->actions == NULL))
            {
                goto fail;
            }
        }
    }

    if (src->phase_count > 0U)
    {
        dst->phases = (engine_phase_t *)calloc(src->phase_count, sizeof(engine_phase_t));
        if (dst->phases == NULL)
        {
            goto fail;
        }

        for (unsigned i = 0U; i < src->phase_count; ++i)
        {
            if (!clone_phase(&dst->phases[i], &src->phases[i]))
            {
                for (unsigned k = 0U; k < i; ++k)
                {
                    free_phase(&dst->phases[k]);
                }
                free(dst->phases);
                dst->phases     = NULL;
                dst->phase_count = 0U;
                goto fail;
            }
        }
    }

    return dst;

fail:
    engine_program_free(dst);
    return NULL;
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

bool engine_step_type_from_str(const char *s, engine_step_type_t *out)
{
    if ((s == NULL) || (out == NULL)) { return false; }
    if (strcmp(s, "event") == 0)    { *out = ENGINE_STEP_EVENT;    return true; }
    if (strcmp(s, "control") == 0)  { *out = ENGINE_STEP_CONTROL;  return true; }
    return false;
}
