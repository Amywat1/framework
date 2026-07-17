/**
 * @file    engine.c
 * @brief   通用控制引擎运行时实现
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "domain/program_engine/engine/engine.h"

#include "domain/program_engine/engine/engine_io.h"

#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 具名常量 */
#define ENGINE_DO_CHANNEL_CAP 64U /* 全部 DO 通道集上限（halt_all 用） */

/* 步骤运行态 */
typedef enum { RT_IDLE = 0, RT_WAIT_AFTER, RT_ARMED, RT_RUNNING, RT_WAIT_DONE, RT_DONE, RT_SKIPPED } rt_step_state_t;

typedef struct {
    rt_step_state_t state;
    unsigned        action_idx;
    bool            waiting;        /* 正在 wait_time 倒计时 */
    uint32_t        wait_remaining; /* wait_time 剩余 ms */
    uint32_t        done_elapsed;   /* done 计时 ms */
    int             trig_prev;      /* signal 触发器上一拍电平 */
} rt_step_t;

typedef struct {
    rt_step_t *steps;
    unsigned   count;
} rt_lane_t;

typedef struct {
    double position;
    bool   valid;
    int    sig_prev;
} marker_rt_t;

struct engine {
    engine_program_t  *prog;
    bool               owns_prog;
    engine_run_state_t state;

    int                   cur_phase;
    const engine_phase_t *cur_def;
    bool                  phase_entered;
    uint32_t              phase_elapsed;

    rt_lane_t *lanes;
    unsigned   lane_count;

    marker_rt_t *markers; /* 与 prog->markers 平行 */

    bool *ilk_active;     /* 与 prog->interlocks 平行 */
    int  *ilk_order;      /* 按 priority 升序的下标 */

    char (*do_channels)[ENGINE_NAME_MAX];
    unsigned do_count;
};

/* -------------------------------------------------------------------------
 * IO 访问
 * ------------------------------------------------------------------------- */
static int sig_read(const char *name)
{
    const engine_io_ops_t *io = engine_io_get_ops();
    return (io != NULL) ? io->read_signal(name) : 0;
}

static void do_write(const char *name, int value)
{
    const engine_io_ops_t *io = engine_io_get_ops();
    if (io != NULL) {
        io->write_output(name, value);
    }
}

/* -------------------------------------------------------------------------
 * 表达式求值环境
 * ------------------------------------------------------------------------- */
static bool engine_resolve(void *ctx, const char *name, double *out)
{
    engine_t *e = (engine_t *)ctx;

    if (name[0] == '$') {
        for (unsigned i = 0U; i < e->prog->param_count; ++i) {
            if (strcmp(e->prog->params[i].name, name + 1) == 0) {
                *out = e->prog->params[i].value;
                return true;
            }
        }
        return false; /* 未知参数 */
    }

    if (strncmp(name, "axes.", 5) == 0) {
        const char *rest = name + 5;
        const char *dot  = strchr(rest, '.');
        if (dot == NULL) {
            return false;
        }

        char   id[ENGINE_NAME_MAX];
        size_t idlen = (size_t)(dot - rest);
        if (idlen >= sizeof(id)) {
            return false;
        }
        memcpy(id, rest, idlen);
        id[idlen] = '\0';

        const engine_io_ops_t *io  = engine_io_get_ops();
        double                 pos = 0.0, speed = 0.0;
        bool                   valid = false;
        if ((io == NULL) || (io->read_axis(id, &pos, &speed, &valid) != SW_OK)) {
            return false;
        }
        const char *field = dot + 1;
        if (strcmp(field, "position") == 0) {
            *out = pos;
            return true;
        }
        if (strcmp(field, "speed") == 0) {
            *out = speed;
            return true;
        }
        if (strcmp(field, "valid") == 0) {
            *out = valid ? 1.0 : 0.0;
            return true;
        }
        return false;
    }

    if (strncmp(name, "markers.", 8) == 0) {
        const char *rest = name + 8;
        const char *dot  = strchr(rest, '.');
        if (dot == NULL) {
            return false;
        }

        size_t idlen = (size_t)(dot - rest);
        for (unsigned i = 0U; i < e->prog->marker_count; ++i) {
            if ((strncmp(e->prog->markers[i].id, rest, idlen) == 0) && (e->prog->markers[i].id[idlen] == '\0')) {
                const char *field = dot + 1;
                if (strcmp(field, "position") == 0) {
                    *out = e->markers[i].position;
                    return true;
                }
                if (strcmp(field, "valid") == 0) {
                    *out = e->markers[i].valid ? 1.0 : 0.0;
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    if (strncmp(name, "phase.", 6) == 0) {
        const char *field = name + 6;
        if (strcmp(field, "elapsed_ms") == 0) {
            *out = (double)e->phase_elapsed;
            return true;
        }
        if (strcmp(field, "direction") == 0) {
            engine_direction_t d = (e->cur_def != NULL) ? e->cur_def->direction : ENGINE_DIR_NONE;
            *out                 = (d == ENGINE_DIR_FORWARD) ? 1.0 : ((d == ENGINE_DIR_BACKWARD) ? -1.0 : 0.0);
            return true;
        }
        return false;
    }

    /* 其余按 DI 信号名（未注册按 0 处理） */
    *out = (double)sig_read(name);
    return true;
}

static bool eval_bool(engine_t *e, const engine_expr_t *x, bool def)
{
    if (x == NULL) {
        return def;
    }
    engine_expr_env_t env = {engine_resolve, e};
    bool              ok  = false;
    bool              v   = engine_expr_eval_bool(x, &env, &ok);
    return ok ? v : def;
}

/* -------------------------------------------------------------------------
 * 边沿检测
 * ------------------------------------------------------------------------- */
static bool edge_hit(engine_edge_t edge, int prev, int cur)
{
    switch (edge) {
    case ENGINE_EDGE_RISING:
        return (prev == 0) && (cur != 0);
    case ENGINE_EDGE_FALLING:
        return (prev != 0) && (cur == 0);
    case ENGINE_EDGE_HIGH:
        return (cur != 0);
    case ENGINE_EDGE_LOW:
        return (cur == 0);
    default:
        return false;
    }
}

/* 触发器在“电平意义”上是否激活（用于 trigger_exit / high / low 判定） */
static bool trigger_level_active(engine_t *e, const engine_trigger_t *trig)
{
    if (trig->type == ENGINE_TRIG_CONDITION) {
        return eval_bool(e, trig->cond, false);
    }
    int cur = sig_read(trig->signal);
    switch (trig->edge) {
    case ENGINE_EDGE_FALLING:
    case ENGINE_EDGE_LOW:
        return (cur == 0);
    default:
        return (cur != 0); /* rising/high */
    }
}

/* -------------------------------------------------------------------------
 * 动作执行
 * ------------------------------------------------------------------------- */
/* 立即执行动作列表（仅 io_set；wait_time 在此上下文忽略，用于 on_enter/on_exit/联锁） */
static void run_actions_now(const engine_action_t *acts, unsigned count)
{
    for (unsigned i = 0U; i < count; ++i) {
        if (acts[i].type == ENGINE_ACT_IO_SET) {
            do_write(acts[i].channel, acts[i].value);
        }
    }
}

/* -------------------------------------------------------------------------
 * halt 处理
 * ------------------------------------------------------------------------- */
static void do_halt_all(engine_t *e)
{
    for (unsigned i = 0U; i < e->do_count; ++i) {
        do_write(e->do_channels[i], 0);
    }
    e->state = ENGINE_STATE_HALTED;
}

static void run_phase_on_exit(engine_t *e);

static void stop_control_outputs(engine_t *e)
{
    const engine_phase_t *ph = e->cur_def;

    if (ph == NULL) {
        return;
    }

    for (unsigned i = 0U; i < ph->lane_count; ++i) {
        for (unsigned j = 0U; j < ph->lanes[i].step_count; ++j) {
            const engine_step_t *sd = &ph->lanes[i].steps[j];
            if ((sd->type == ENGINE_STEP_CONTROL) && (sd->output[0] != '\0')) {
                do_write(sd->output, 0);
            }
        }
    }
}

static void run_phase_on_exit(engine_t *e)
{
    stop_control_outputs(e);
    if (e->cur_def != NULL) {
        run_actions_now(e->cur_def->on_exit, e->cur_def->on_exit_count);
    }
}

static void do_halt_phase(engine_t *e)
{
    run_phase_on_exit(e);
    e->state = ENGINE_STATE_PHASE_HALTED;
}

static void apply_on_error(engine_t *e, engine_error_strategy_t st, rt_step_t *rt)
{
    switch (st) {
    case ENGINE_ERR_STOP:
        do_halt_all(e);
        break;
    case ENGINE_ERR_HALT_PHASE:
        do_halt_phase(e);
        break;
    case ENGINE_ERR_SKIP:
        if (rt != NULL) {
            rt->state = RT_SKIPPED;
        }
        break;
    case ENGINE_ERR_DEGRADE:
        if (rt != NULL) {
            rt->state = RT_DONE;
        }
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------------------
 * 阶段运行态分配/释放
 * ------------------------------------------------------------------------- */
static void free_lanes(engine_t *e)
{
    if (e->lanes != NULL) {
        for (unsigned i = 0U; i < e->lane_count; ++i) {
            free(e->lanes[i].steps);
        }
        free(e->lanes);
        e->lanes = NULL;
    }
    e->lane_count = 0U;
}

/* 进入阶段 i：分配运行态、初始化步骤（不求 entry_guard、不执行 on_enter） */
static bool enter_phase(engine_t *e, int idx)
{
    free_lanes(e);

    e->cur_phase     = idx;
    e->cur_def       = &e->prog->phases[idx];
    e->phase_entered = false;
    e->phase_elapsed = 0U;

    const engine_phase_t *ph = e->cur_def;
    e->lanes                 = (rt_lane_t *)calloc(ph->lane_count, sizeof(rt_lane_t));
    if (e->lanes == NULL) {
        return false;
    }
    e->lane_count = ph->lane_count;

    for (unsigned i = 0U; i < ph->lane_count; ++i) {
        unsigned sc       = ph->lanes[i].step_count;
        e->lanes[i].steps = (rt_step_t *)calloc((sc > 0U) ? sc : 1U, sizeof(rt_step_t));
        if (e->lanes[i].steps == NULL) {
            return false;
        }
        e->lanes[i].count = sc;
        for (unsigned j = 0U; j < sc; ++j) {
            const engine_step_t *sd = &ph->lanes[i].steps[j];
            rt_step_t           *rt = &e->lanes[i].steps[j];

            if (sd->type == ENGINE_STEP_CONTROL) {
                rt->state = RT_DONE;
                continue;
            }

            rt->state          = (sd->after_count > 0U) ? RT_WAIT_AFTER : RT_ARMED;
            rt->action_idx     = 0U;
            rt->waiting        = false;
            rt->wait_remaining = 0U;
            rt->done_elapsed   = 0U;
            rt->trig_prev      = (sd->trigger.type == ENGINE_TRIG_SIGNAL) ? sig_read(sd->trigger.signal) : 0;
        }
    }
    return true;
}

/* 在当前阶段按 step id 找运行态（after 依赖判定用） */
static const rt_step_t *find_rt_by_id(engine_t *e, const char *id)
{
    const engine_phase_t *ph = e->cur_def;
    for (unsigned i = 0U; i < ph->lane_count; ++i) {
        for (unsigned j = 0U; j < ph->lanes[i].step_count; ++j) {
            if (strcmp(ph->lanes[i].steps[j].id, id) == 0) {
                return &e->lanes[i].steps[j];
            }
        }
    }
    return NULL;
}

static bool after_deps_met(engine_t *e, const engine_step_t *sd)
{
    for (unsigned i = 0U; i < sd->after_count; ++i) {
        const rt_step_t *dep = find_rt_by_id(e, sd->after[i]);
        if (dep == NULL) {
            return false;
        }
        if ((dep->state != RT_DONE) && (dep->state != RT_SKIPPED)) {
            return false;
        }
    }
    return true;
}

/* -------------------------------------------------------------------------
 * control 型步骤：每拍按 active_while / value_expr 写 DO
 * ------------------------------------------------------------------------- */
static void control_step_tick(engine_t *e, const engine_step_t *sd)
{
    engine_expr_env_t env = {.resolve = engine_resolve, .ctx = e};

    if (eval_bool(e, sd->active_while, false)) {
        bool   ok = false;
        double v  = engine_expr_eval(sd->value_expr, &env, &ok);
        if (!ok) {
            apply_on_error(e, sd->on_error, NULL);
            return;
        }
        do_write(sd->output, (int)v);
    } else {
        do_write(sd->output, 0);
    }
}

/* -------------------------------------------------------------------------
 * 单步骤推进
 * ------------------------------------------------------------------------- */
static void step_tick(engine_t *e, const engine_step_t *sd, rt_step_t *rt, uint32_t dt)
{
    int cur_sig = (sd->trigger.type == ENGINE_TRIG_SIGNAL) ? sig_read(sd->trigger.signal) : 0;

    /* 单拍内允许状态级联推进（WAIT_AFTER→ARMED→RUNNING→…），减少逐拍延迟 */
    bool progressed = true;
    while (progressed && (e->state == ENGINE_STATE_RUNNING)) {
        progressed = false;

        switch (rt->state) {
        case RT_WAIT_AFTER:
            if (after_deps_met(e, sd)) {
                rt->state  = RT_ARMED;
                progressed = true;
            }
            break;

        case RT_ARMED: {
            bool fire;
            if (sd->trigger.type == ENGINE_TRIG_CONDITION) {
                fire = eval_bool(e, sd->trigger.cond, false);
            } else {
                fire = edge_hit(sd->trigger.edge, rt->trig_prev, cur_sig);
            }
            if (fire && ((sd->guard == NULL) || eval_bool(e, sd->guard, false))) {
                /* 触发后再判守卫；守卫不过则保持 ARMED 等待下一拍 */
                rt->state      = RT_RUNNING;
                rt->action_idx = 0U;
                rt->waiting    = false;
                progressed     = true;
            }
            break;
        }

        case RT_RUNNING: {
            /* wait_time 倒计时（跨拍） */
            if (rt->waiting) {
                if (rt->wait_remaining <= dt) {
                    rt->waiting        = false;
                    rt->wait_remaining = 0U;
                    ++rt->action_idx;
                } else {
                    rt->wait_remaining -= dt;
                    break; /* 本拍继续等待 */
                }
            }
            /* 顺序执行动作 */
            while (rt->action_idx < sd->action_count) {
                const engine_action_t *act = &sd->actions[rt->action_idx];
                if (act->type == ENGINE_ACT_IO_SET) {
                    do_write(act->channel, act->value);
                    ++rt->action_idx;
                } else /* wait_time */
                {
                    rt->waiting        = true;
                    rt->wait_remaining = act->ms;
                    break;
                }
            }
            if ((rt->action_idx >= sd->action_count) && !rt->waiting) {
                if (sd->done.type == ENGINE_DONE_ACTIONS_COMPLETE) {
                    rt->state = RT_DONE;
                } else {
                    rt->state        = RT_WAIT_DONE;
                    rt->done_elapsed = 0U;
                }
                progressed = true;
            }
            break;
        }

        case RT_WAIT_DONE: {
            switch (sd->done.type) {
            case ENGINE_DONE_TRIGGER_EXIT:
                if (!trigger_level_active(e, &sd->trigger)) {
                    rt->state = RT_DONE;
                }
                break;

            case ENGINE_DONE_SIGNAL: {
                int v = sig_read(sd->done.signal);
                if (v == sd->done.state) {
                    rt->state = RT_DONE;
                } else {
                    rt->done_elapsed += dt;
                    if ((sd->done.timeout_ms > 0U) && (rt->done_elapsed >= sd->done.timeout_ms)) {
                        apply_on_error(e, sd->on_error, rt);
                    }
                }
                break;
            }

            case ENGINE_DONE_TIMEOUT:
                rt->done_elapsed += dt;
                if (rt->done_elapsed >= sd->done.timeout_ms) {
                    rt->state = RT_DONE;
                }
                break;

            default:
                rt->state = RT_DONE;
                break;
            }
            break;
        }

        default:
            break; /* RT_DONE / RT_SKIPPED / RT_IDLE 稳定 */
        }
    }

    /* 刷新 signal 触发器上一拍电平 */
    if (sd->trigger.type == ENGINE_TRIG_SIGNAL) {
        rt->trig_prev = cur_sig;
    }
}

/* -------------------------------------------------------------------------
 * 标记锁存
 * ------------------------------------------------------------------------- */
static void markers_tick(engine_t *e)
{
    const engine_io_ops_t *io = engine_io_get_ops();
    for (unsigned i = 0U; i < e->prog->marker_count; ++i) {
        const engine_marker_t *mk  = &e->prog->markers[i];
        marker_rt_t           *rt  = &e->markers[i];
        int                    cur = sig_read(mk->signal);
        if (!rt->valid && edge_hit(mk->edge, rt->sig_prev, cur)) {
            double pos = 0.0, speed = 0.0;
            bool   valid = false;
            if ((io != NULL) && (io->read_axis(mk->axis, &pos, &speed, &valid) == SW_OK)) {
                rt->position = pos;
                rt->valid    = true;
            }
        }
        rt->sig_prev = cur;
    }
}

/* -------------------------------------------------------------------------
 * 联锁扫描
 * ------------------------------------------------------------------------- */
static void interlocks_tick(engine_t *e)
{
    for (unsigned k = 0U; k < e->prog->interlock_count; ++k) {
        int                       idx = e->ilk_order[k];
        const engine_interlock_t *il  = &e->prog->interlocks[idx];

        if (!e->ilk_active[idx]) {
            if (eval_bool(e, il->condition, false)) {
                e->ilk_active[idx] = true;
                switch (il->action) {
                case ENGINE_ILK_HALT_ALL:
                    do_halt_all(e);
                    return; /* 程序终止，停止后续扫描 */
                case ENGINE_ILK_HALT_PHASE:
                    /* 已处于暂停态时不重复执行 on_exit：
                     * auto_reset 联锁在 reset→再触发 的循环里会重入此处，
                     * 但等待期间阶段已暂停，不应再执行 on_exit 动作列表 */
                    if (e->state != ENGINE_STATE_PHASE_HALTED) {
                        do_halt_phase(e);
                    }
                    return;
                case ENGINE_ILK_CUSTOM:
                    run_actions_now(il->actions, il->action_count);
                    break;
                default:
                    break;
                }
            }
        } else {
            if (il->auto_reset && eval_bool(e, il->reset_condition, false)) {
                e->ilk_active[idx] = false;
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * 阶段推进
 * ------------------------------------------------------------------------- */
static void advance_phase(engine_t *e)
{
    int next = e->cur_phase + 1;
    if (next >= (int)e->prog->phase_count) {
        free_lanes(e);
        e->cur_def   = NULL;
        e->cur_phase = next;
        e->state     = ENGINE_STATE_DONE;
        return;
    }
    if (!enter_phase(e, next)) {
        e->state = ENGINE_STATE_HALTED;
    }
}

static void phase_tick(engine_t *e, uint32_t dt)
{
    const engine_phase_t *ph = e->cur_def;

    /* 阶段进入：等待 entry_guard，满足后执行 on_enter 一次 */
    if (!e->phase_entered) {
        if (!eval_bool(e, ph->entry_guard, false)) {
            return; /* 仍在等待进入 */
        }
        run_actions_now(ph->on_enter, ph->on_enter_count);
        e->phase_entered = true;
    }

    /* 并行推进所有通道步骤 */
    for (unsigned i = 0U; i < e->lane_count; ++i) {
        for (unsigned j = 0U; j < e->lanes[i].count; ++j) {
            const engine_step_t *sd = &ph->lanes[i].steps[j];
            rt_step_t           *rt = &e->lanes[i].steps[j];

            if (sd->type == ENGINE_STEP_CONTROL) {
                control_step_tick(e, sd);
            } else {
                step_tick(e, sd, rt, dt);
            }
            if (e->state != ENGINE_STATE_RUNNING) {
                return;
            }
        }
    }

    /* 计时与超时 */
    e->phase_elapsed += dt;
    if ((ph->timeout_ms > 0U) && (e->phase_elapsed >= ph->timeout_ms)) {
        if (ph->on_timeout == ENGINE_ERR_STOP) {
            do_halt_all(e);
        } else {
            do_halt_phase(e);
        }
        return;
    }

    /* 退出判定 */
    if (eval_bool(e, ph->exit_guard, false)) {
        run_phase_on_exit(e);
        advance_phase(e);
    }
}

/* -------------------------------------------------------------------------
 * 公开接口
 * ------------------------------------------------------------------------- */
engine_t *engine_create(void)
{
    engine_t *e = (engine_t *)calloc(1U, sizeof(engine_t));
    if (e != NULL) {
        e->state     = ENGINE_STATE_IDLE;
        e->cur_phase = -1;
    }
    return e;
}

/* 收集方案中全部 DO 通道名（halt_all 用） */
static void collect_do_channel(engine_t *e, const char *name, unsigned *overflow)
{
    if ((name == NULL) || (name[0] == '\0')) {
        return;
    }
    for (unsigned i = 0U; i < e->do_count; ++i) {
        if (strcmp(e->do_channels[i], name) == 0) {
            return;
        }
    }
    if (e->do_count < ENGINE_DO_CHANNEL_CAP) {
        (void)snprintf(e->do_channels[e->do_count], ENGINE_NAME_MAX, "%s", name);
        ++e->do_count;
    } else {
        ++(*overflow);
    }
}

static void collect_actions_channels(engine_t *e, const engine_action_t *a, unsigned n, unsigned *overflow)
{
    for (unsigned i = 0U; i < n; ++i) {
        if (a[i].type == ENGINE_ACT_IO_SET) {
            collect_do_channel(e, a[i].channel, overflow);
        }
    }
}

static sw_err_t build_runtime_tables(engine_t *e)
{
    engine_program_t *p = e->prog;

    if (p->marker_count > 0U) {
        e->markers = (marker_rt_t *)calloc(p->marker_count, sizeof(marker_rt_t));
        if (e->markers == NULL) {
            return SW_ERR_NOMEM;
        }
    }
    if (p->interlock_count > 0U) {
        e->ilk_active = (bool *)calloc(p->interlock_count, sizeof(bool));
        e->ilk_order  = (int *)calloc(p->interlock_count, sizeof(int));
        if ((e->ilk_active == NULL) || (e->ilk_order == NULL)) {
            return SW_ERR_NOMEM;
        }
        for (unsigned i = 0U; i < p->interlock_count; ++i) {
            e->ilk_order[i] = (int)i;
        }
        /* 按 priority 升序排序（插入排序，数量极小） */
        for (unsigned i = 1U; i < p->interlock_count; ++i) {
            int key = e->ilk_order[i];
            int kp  = p->interlocks[key].priority;
            int j   = (int)i - 1;
            while ((j >= 0) && (p->interlocks[e->ilk_order[j]].priority > kp)) {
                e->ilk_order[j + 1] = e->ilk_order[j];
                --j;
            }
            e->ilk_order[j + 1] = key;
        }
    }

    e->do_channels = (char(*)[ENGINE_NAME_MAX])calloc(ENGINE_DO_CHANNEL_CAP, ENGINE_NAME_MAX);
    if (e->do_channels == NULL) {
        return SW_ERR_NOMEM;
    }
    e->do_count = 0U;

    unsigned overflow_count = 0U;
    for (unsigned i = 0U; i < p->phase_count; ++i) {
        const engine_phase_t *ph = &p->phases[i];
        collect_actions_channels(e, ph->on_enter, ph->on_enter_count, &overflow_count);
        collect_actions_channels(e, ph->on_exit, ph->on_exit_count, &overflow_count);
        for (unsigned l = 0U; l < ph->lane_count; ++l) {
            for (unsigned s = 0U; s < ph->lanes[l].step_count; ++s) {
                const engine_step_t *st = &ph->lanes[l].steps[s];
                collect_actions_channels(e, st->actions, st->action_count, &overflow_count);
                if ((st->type == ENGINE_STEP_CONTROL) && (st->output[0] != '\0')) {
                    collect_do_channel(e, st->output, &overflow_count);
                }
            }
        }
    }
    for (unsigned i = 0U; i < p->interlock_count; ++i) {
        collect_actions_channels(e, p->interlocks[i].actions, p->interlocks[i].action_count, &overflow_count);
    }

    if (overflow_count > 0U) {
        LOG_ERROR("DO 通道数超出上限：方案共 %u 个唯一通道，上限为 %u",
                  ENGINE_DO_CHANNEL_CAP + overflow_count, ENGINE_DO_CHANNEL_CAP);
        return SW_ERR_OVERFLOW;
    }
    return SW_OK;
}

static void free_runtime_tables(engine_t *e)
{
    free(e->markers);
    e->markers = NULL;
    free(e->ilk_active);
    e->ilk_active = NULL;
    free(e->ilk_order);
    e->ilk_order = NULL;
    free(e->do_channels);
    e->do_channels = NULL;
    e->do_count    = 0U;
}

sw_err_t engine_load_program(engine_t *e, engine_program_t *prog)
{
    if ((e == NULL) || (prog == NULL)) {
        return SW_ERR_PARAM;
    }

    if (e->owns_prog && (e->prog != NULL)) {
        engine_program_free(e->prog);
    }
    free_lanes(e);
    free_runtime_tables(e);

    e->prog      = prog;
    e->owns_prog = true;
    e->state     = ENGINE_STATE_IDLE;
    e->cur_phase = -1;
    e->cur_def   = NULL;

    sw_err_t rc = build_runtime_tables(e);
    if (rc != SW_OK) {
        return rc;
    }
    return SW_OK;
}

sw_err_t engine_start(engine_t *e)
{
    if ((e == NULL) || (e->prog == NULL)) {
        return SW_ERR_STATE;
    }
    if (e->prog->phase_count == 0U) {
        return SW_ERR_STATE;
    }

    /* 复位标记与联锁 */
    for (unsigned i = 0U; i < e->prog->marker_count; ++i) {
        e->markers[i].position = 0.0;
        e->markers[i].valid    = false;
        e->markers[i].sig_prev = sig_read(e->prog->markers[i].signal);
    }
    for (unsigned i = 0U; i < e->prog->interlock_count; ++i) {
        e->ilk_active[i] = false;
    }

    e->state = ENGINE_STATE_RUNNING;
    if (!enter_phase(e, 0)) {
        e->state = ENGINE_STATE_HALTED;
        return SW_ERR_NOMEM;
    }
    return SW_OK;
}

void engine_tick(engine_t *e, uint32_t dt_ms)
{
    if ((e == NULL) || (e->prog == NULL)) {
        return;
    }

    /* 标记锁存（即使非 RUNNING 也维持边沿快照，但仅 RUNNING 期间有意义） */
    if ((e->state == ENGINE_STATE_RUNNING) || (e->state == ENGINE_STATE_PHASE_HALTED)) {
        markers_tick(e);
    }

    /* 联锁：仅在执行期监控；IDLE/DONE/HALTED 下不运行，避免改写终态 */
    if ((e->state == ENGINE_STATE_RUNNING) || (e->state == ENGINE_STATE_PHASE_HALTED)) {
        interlocks_tick(e);
    }

    if (e->state != ENGINE_STATE_RUNNING) {
        return;
    }

    phase_tick(e, dt_ms);
}

sw_err_t engine_recover(engine_t *e)
{
    if ((e == NULL) || (e->state != ENGINE_STATE_PHASE_HALTED)) {
        return SW_ERR_STATE;
    }

    /* 重置 halt_phase 联锁的活跃标志，强制下一拍重新评估条件。
     * 若碰撞仍在 → 立即再触发 PHASE_HALTED；
     * 若已消除  → 正常恢复执行。
     * halt_all 联锁不重置（其触发结果为 HALTED，走不到此处）。 */
    for (unsigned i = 0U; i < e->prog->interlock_count; ++i) {
        if (e->prog->interlocks[i].action == ENGINE_ILK_HALT_PHASE) {
            e->ilk_active[i] = false;
        }
    }

    /* 保留 DONE/SKIPPED 步骤，将中途暂停的步骤回退至 ARMED 使其重新执行动作输出。
     * on_exit 已在 do_halt_phase 时执行，此处重新 on_enter 恢复阶段初始输出。
     * 不重置 phase_elapsed，避免每次 recover 重启超时时钟（最大时长不得扩大）。*/
    for (unsigned i = 0U; i < e->lane_count; ++i) {
        for (unsigned j = 0U; j < e->lanes[i].count; ++j) {
            rt_step_t *rt = &e->lanes[i].steps[j];
            if ((rt->state == RT_RUNNING) || (rt->state == RT_WAIT_DONE)) {
                rt->state          = RT_ARMED;
                rt->action_idx     = 0U;
                rt->waiting        = false;
                rt->wait_remaining = 0U;
                rt->done_elapsed   = 0U;
            }
        }
    }

    if (e->cur_def != NULL) {
        run_actions_now(e->cur_def->on_enter, e->cur_def->on_enter_count);
    }

    e->phase_entered = true;
    e->state         = ENGINE_STATE_RUNNING;
    return SW_OK;
}

engine_run_state_t engine_state(const engine_t *e)
{
    return (e != NULL) ? e->state : ENGINE_STATE_IDLE;
}

int engine_current_phase(const engine_t *e)
{
    if ((e == NULL) || (e->state == ENGINE_STATE_IDLE)) {
        return -1;
    }
    return e->cur_phase;
}

const char *engine_current_phase_id(const engine_t *e)
{
    if ((e == NULL) || (e->cur_def == NULL)) {
        return NULL;
    }
    return e->cur_def->id;
}

void engine_destroy(engine_t *e)
{
    if (e == NULL) {
        return;
    }
    free_lanes(e);
    free_runtime_tables(e);
    if (e->owns_prog && (e->prog != NULL)) {
        engine_program_free(e->prog);
    }
    free(e);
}

engine_direction_t engine_current_direction(const engine_t *e)
{
    if ((e == NULL) || (e->cur_def == NULL)) {
        return ENGINE_DIR_NONE;
    }
    return e->cur_def->direction;
}
