/**
 * @file    engine_program_validate.c
 * @brief   控制方案语义校验实现
 * @author  huwangwei
 * @date    2026-07-08
 */

#include "domain/program_engine/model/engine_program_validate.h"

#include "domain/program_engine/engine/engine_expr.h"

#include <stdio.h>
#include <string.h>

#define VAL_ERR_MAX 160U

static void vfail(char *err, unsigned errsz, const char *fmt, const char *arg)
{
    if ((err != NULL) && (errsz > 0U)) {
        (void)snprintf(err, errsz, fmt, arg);
    }
}

static bool name_in_list(const char *name, const char *const *list, unsigned count)
{
    if ((name == NULL) || (list == NULL)) {
        return false;
    }
    for (unsigned i = 0U; i < count; ++i) {
        if ((list[i] != NULL) && (strcmp(list[i], name) == 0)) {
            return true;
        }
    }
    return false;
}

static bool prog_has_axis(const engine_program_t *prog, const char *id)
{
    for (unsigned i = 0U; i < prog->axis_count; ++i) {
        if (strcmp(prog->axes[i].id, id) == 0) {
            return true;
        }
    }
    return false;
}

static bool prog_has_marker(const engine_program_t *prog, const char *id)
{
    for (unsigned i = 0U; i < prog->marker_count; ++i) {
        if (strcmp(prog->markers[i].id, id) == 0) {
            return true;
        }
    }
    return false;
}

static bool prog_has_param(const engine_program_t *prog, const char *name)
{
    for (unsigned i = 0U; i < prog->param_count; ++i) {
        if (strcmp(prog->params[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool phase_has_step(const engine_phase_t *ph, const char *id)
{
    for (unsigned l = 0U; l < ph->lane_count; ++l) {
        const engine_lane_t *lane = &ph->lanes[l];
        for (unsigned s = 0U; s < lane->step_count; ++s) {
            if (strcmp(lane->steps[s].id, id) == 0) {
                return true;
            }
        }
    }
    return false;
}

typedef struct {
    const engine_program_t          *prog;
    const engine_io_catalog_t       *io_catalog;
    const engine_actuator_catalog_t *act_catalog;
    const engine_variable_catalog_t *var_catalog;
    char                            *err;
    unsigned                         errsz;
    bool                             ok;
} var_ctx_t;

static bool resolve_var_name(const char *name, var_ctx_t *ctx)
{
    if ((name == NULL) || (ctx == NULL)) {
        return false;
    }

    if (name[0] == '$') {
        if (!prog_has_param(ctx->prog, name + 1)) {
            vfail(ctx->err, ctx->errsz, "未知参数: %s", name);
            ctx->ok = false;
            return false;
        }
        return true;
    }

    if (strcmp(name, "phase.elapsed_ms") == 0) {
        return true;
    }
    if (strcmp(name, "phase.direction") == 0) {
        return true;
    }

    if (strncmp(name, "axes.", 5) == 0) {
        const char *rest = name + 5;
        const char *dot  = strchr(rest, '.');
        if (dot == NULL) {
            vfail(ctx->err, ctx->errsz, "轴引用格式错误: %s", name);
            ctx->ok = false;
            return false;
        }
        char   axis_id[ENGINE_NAME_MAX];
        size_t len = (size_t)(dot - rest);
        if ((len == 0U) || (len >= ENGINE_NAME_MAX)) {
            vfail(ctx->err, ctx->errsz, "轴引用格式错误: %s", name);
            ctx->ok = false;
            return false;
        }
        (void)memcpy(axis_id, rest, len);
        axis_id[len] = '\0';

        if (!prog_has_axis(ctx->prog, axis_id)) {
            vfail(ctx->err, ctx->errsz, "未知坐标轴: %s", axis_id);
            ctx->ok = false;
            return false;
        }
        return true;
    }

    if (strncmp(name, "markers.", 8) == 0) {
        const char *rest = name + 8;
        const char *dot  = strchr(rest, '.');
        if (dot == NULL) {
            vfail(ctx->err, ctx->errsz, "标记引用格式错误: %s", name);
            ctx->ok = false;
            return false;
        }
        char   marker_id[ENGINE_NAME_MAX];
        size_t len = (size_t)(dot - rest);
        if ((len == 0U) || (len >= ENGINE_NAME_MAX)) {
            vfail(ctx->err, ctx->errsz, "标记引用格式错误: %s", name);
            ctx->ok = false;
            return false;
        }
        (void)memcpy(marker_id, rest, len);
        marker_id[len] = '\0';

        if (!prog_has_marker(ctx->prog, marker_id)) {
            vfail(ctx->err, ctx->errsz, "未知位置标记: %s", marker_id);
            ctx->ok = false;
            return false;
        }
        return true;
    }

    if ((ctx->io_catalog != NULL) && name_in_list(name, ctx->io_catalog->signals, ctx->io_catalog->signal_count)) {
        return true;
    }

    if ((ctx->var_catalog != NULL) && name_in_list(name, ctx->var_catalog->names, ctx->var_catalog->name_count)) {
        return true;
    }

    if (ctx->io_catalog != NULL) {
        vfail(ctx->err, ctx->errsz, "未知 DI 信号: %s", name);
        ctx->ok = false;
        return false;
    }

    return true;
}

static bool var_cb(const char *name, void *ctx)
{
    var_ctx_t *v = (var_ctx_t *)ctx;
    return resolve_var_name(name, v);
}

static void validate_expr(const engine_expr_t *expr, var_ctx_t *ctx)
{
    if ((expr == NULL) || !ctx->ok) {
        return;
    }
    (void)engine_expr_foreach_var(expr, var_cb, ctx);
}

static void validate_intent(const engine_intent_t *intent, var_ctx_t *ctx)
{
    if ((intent == NULL) || !ctx->ok) {
        return;
    }
    if (intent->resource[0] == '\0') {
        vfail(ctx->err, ctx->errsz, "%s", "意图缺少 resource");
        ctx->ok = false;
        return;
    }
    if (intent->cmd[0] == '\0') {
        vfail(ctx->err, ctx->errsz, "%s", "意图缺少 cmd");
        ctx->ok = false;
        return;
    }
    if ((ctx->act_catalog != NULL)
        && !name_in_list(intent->resource, ctx->act_catalog->resources, ctx->act_catalog->resource_count)) {
        vfail(ctx->err, ctx->errsz, "未知执行机构资源: %s", intent->resource);
        ctx->ok = false;
        return;
    }
    for (unsigned p = 0U; (p < intent->path_count) && ctx->ok; ++p) {
        if ((ctx->act_catalog != NULL) && (ctx->act_catalog->water_paths != NULL)
            && !name_in_list(intent->paths[p], ctx->act_catalog->water_paths, ctx->act_catalog->water_path_count)) {
            vfail(ctx->err, ctx->errsz, "未知水路路径: %s", intent->paths[p]);
            ctx->ok = false;
            return;
        }
    }
}

static void validate_actions(const engine_action_t *acts, unsigned count, var_ctx_t *ctx)
{
    for (unsigned i = 0U; (i < count) && ctx->ok; ++i) {
        if (acts[i].type == ENGINE_ACT_INTENT) {
            validate_intent(&acts[i].intent, ctx);
        }
    }
}

static void validate_step_signals(const engine_step_t *st, var_ctx_t *ctx)
{
    if (st->trigger.type == ENGINE_TRIG_SIGNAL) {
        if ((ctx->io_catalog != NULL)
            && !name_in_list(st->trigger.signal, ctx->io_catalog->signals, ctx->io_catalog->signal_count)) {
            vfail(ctx->err, ctx->errsz, "未知触发信号: %s", st->trigger.signal);
            ctx->ok = false;
            return;
        }
    }

    if ((st->done.type == ENGINE_DONE_SIGNAL) && ctx->ok) {
        if ((ctx->io_catalog != NULL)
            && !name_in_list(st->done.signal, ctx->io_catalog->signals, ctx->io_catalog->signal_count)) {
            vfail(ctx->err, ctx->errsz, "未知 done 信号: %s", st->done.signal);
            ctx->ok = false;
        }
    }
}

static bool phase_id_unique(const engine_program_t *prog, char *err, unsigned errsz)
{
    for (unsigned i = 0U; i < prog->phase_count; ++i) {
        for (unsigned j = i + 1U; j < prog->phase_count; ++j) {
            if (strcmp(prog->phases[i].id, prog->phases[j].id) == 0) {
                vfail(err, errsz, "阶段 id 重复: %s", prog->phases[i].id);
                return false;
            }
        }
    }
    return true;
}

static bool has_estop_interlock(const engine_program_t *prog)
{
    for (unsigned i = 0U; i < prog->interlock_count; ++i) {
        if (strcmp(prog->interlocks[i].id, "estop") == 0) {
            return true;
        }
    }
    return false;
}

sw_err_t engine_program_validate(const engine_program_t          *prog,
                                 const engine_io_catalog_t       *io_catalog,
                                 const engine_actuator_catalog_t *act_catalog,
                                 const engine_variable_catalog_t *var_catalog,
                                 char                            *err,
                                 unsigned                         errsz)
{
    char     local_err[VAL_ERR_MAX];
    char    *werr = (err != NULL && errsz > 0U) ? err : local_err;
    unsigned wsz  = (err != NULL && errsz > 0U) ? errsz : (unsigned)sizeof(local_err);
    werr[0]       = '\0';

    if (prog == NULL) {
        vfail(werr, wsz, "%s", "方案为空");
        return SW_ERR_PARAM;
    }

    if ((prog->phase_count == 0U) || (prog->phases == NULL)) {
        vfail(werr, wsz, "%s", "方案缺少 phases");
        return SW_ERR_PARAM;
    }

    if (!phase_id_unique(prog, werr, wsz)) {
        return SW_ERR_PARAM;
    }

    if (!has_estop_interlock(prog)) {
        vfail(werr, wsz, "%s", "缺少 estop 联锁");
        return SW_ERR_PARAM;
    }

    for (unsigned a = 0U; a < prog->axis_count; ++a) {
        if ((io_catalog != NULL) && !name_in_list(prog->axes[a].id, io_catalog->axes, io_catalog->axis_count)) {
            vfail(werr, wsz, "方案引用未知物理轴: %s", prog->axes[a].id);
            return SW_ERR_PARAM;
        }
    }

    for (unsigned m = 0U; m < prog->marker_count; ++m) {
        const engine_marker_t *mk = &prog->markers[m];
        if (!prog_has_axis(prog, mk->axis)) {
            vfail(werr, wsz, "标记引用未知轴: %s", mk->axis);
            return SW_ERR_PARAM;
        }
        if (mk->on_kind == ENGINE_MARKER_ON_SIGNAL) {
            if ((io_catalog != NULL) && !name_in_list(mk->signal, io_catalog->signals, io_catalog->signal_count)) {
                vfail(werr, wsz, "标记引用未知信号: %s", mk->signal);
                return SW_ERR_PARAM;
            }
        } else if (mk->on_kind == ENGINE_MARKER_ON_CONDITION) {
            if (mk->cond == NULL) {
                vfail(werr, wsz, "标记缺少条件表达式: %s", mk->id);
                return SW_ERR_PARAM;
            }
        } else {
            vfail(werr, wsz, "标记触发源非法: %s", mk->id);
            return SW_ERR_PARAM;
        }
    }

    var_ctx_t vctx = {
        .prog        = prog,
        .io_catalog  = io_catalog,
        .act_catalog = act_catalog,
        .var_catalog = var_catalog,
        .err         = werr,
        .errsz       = wsz,
        .ok          = true,
    };

    for (unsigned m = 0U; m < prog->marker_count; ++m) {
        const engine_marker_t *mk = &prog->markers[m];
        if (mk->on_kind == ENGINE_MARKER_ON_CONDITION) {
            validate_expr(mk->cond, &vctx);
            if (!vctx.ok) {
                return SW_ERR_PARAM;
            }
        }
    }

    for (unsigned i = 0U; i < prog->interlock_count; ++i) {
        const engine_interlock_t *ilk = &prog->interlocks[i];
        validate_expr(ilk->condition, &vctx);
        validate_expr(ilk->reset_condition, &vctx);
        validate_actions(ilk->actions, ilk->action_count, &vctx);
        if (!vctx.ok) {
            return SW_ERR_PARAM;
        }
    }

    for (unsigned pi = 0U; pi < prog->phase_count; ++pi) {
        const engine_phase_t *ph = &prog->phases[pi];

        if (ph->timeout_ms == 0U) {
            vfail(werr, wsz, "阶段 timeout_ms 为 0: %s", ph->id);
            return SW_ERR_PARAM;
        }

        validate_expr(ph->entry_guard, &vctx);
        validate_expr(ph->exit_guard, &vctx);
        validate_actions(ph->on_enter, ph->on_enter_count, &vctx);
        validate_actions(ph->on_exit, ph->on_exit_count, &vctx);
        if (!vctx.ok) {
            return SW_ERR_PARAM;
        }
        if (act_catalog != NULL) {
            for (unsigned k = 0U; k < ph->keep_count; ++k) {
                if (!name_in_list(ph->keep[k], act_catalog->resources, act_catalog->resource_count)) {
                    vfail(werr, wsz, "未知 keep 资源: %s", ph->keep[k]);
                    return SW_ERR_PARAM;
                }
            }
        }

        for (unsigned l = 0U; l < ph->lane_count; ++l) {
            const engine_lane_t *lane = &ph->lanes[l];
            for (unsigned s = 0U; s < lane->step_count; ++s) {
                const engine_step_t *st = &lane->steps[s];

                if (st->type == ENGINE_STEP_CONTROL) {
                    validate_expr(st->active_while, &vctx);
                    validate_intent(&st->intent, &vctx);
                    if (!vctx.ok) {
                        return SW_ERR_PARAM;
                    }
                    continue;
                }

                validate_expr(st->trigger.cond, &vctx);
                validate_expr(st->guard, &vctx);
                validate_actions(st->actions, st->action_count, &vctx);
                validate_step_signals(st, &vctx);
                if (!vctx.ok) {
                    return SW_ERR_PARAM;
                }

                for (unsigned a = 0U; a < st->after_count; ++a) {
                    if (!phase_has_step(ph, st->after[a])) {
                        vfail(werr, wsz, "after 引用未知步骤: %s", st->after[a]);
                        return SW_ERR_PARAM;
                    }
                }
            }
        }
    }

    return SW_OK;
}
