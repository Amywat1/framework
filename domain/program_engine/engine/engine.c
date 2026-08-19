/**
 * @file    engine.c
 * @brief   通用控制引擎运行时实现
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "domain/program_engine/engine/engine.h"

#include "domain/program_engine/model/engine_program_validate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 具名常量 */
#define ENGINE_HELD_CAP 64U /* 本阶段持有资源上限 */

#define ENGINE_TOKEN_KIND_SHIFT 28U
#define ENGINE_TOKEN_INDEX_MASK 0x0FFFFFFFU

typedef enum {
    ENGINE_TOKEN_PARAM = 0U,
    ENGINE_TOKEN_AXIS_POSITION,
    ENGINE_TOKEN_AXIS_SPEED,
    ENGINE_TOKEN_AXIS_VALID,
    ENGINE_TOKEN_MARKER_POSITION,
    ENGINE_TOKEN_MARKER_VALID,
    ENGINE_TOKEN_PHASE_ELAPSED,
    ENGINE_TOKEN_PHASE_DIRECTION,
    ENGINE_TOKEN_VARIABLE,
    ENGINE_TOKEN_SIGNAL
} engine_token_kind_t;

static unsigned make_token(engine_token_kind_t kind, unsigned index)
{
    return ((unsigned)kind << ENGINE_TOKEN_KIND_SHIFT) | (index & ENGINE_TOKEN_INDEX_MASK);
}

static engine_token_kind_t token_kind(unsigned token)
{
    return (engine_token_kind_t)(token >> ENGINE_TOKEN_KIND_SHIFT);
}

static unsigned token_index(unsigned token)
{
    return token & ENGINE_TOKEN_INDEX_MASK;
}

/* 步骤运行态 */
typedef enum { RT_IDLE = 0, RT_WAIT_AFTER, RT_ARMED, RT_RUNNING, RT_WAIT_DONE, RT_DONE, RT_SKIPPED } rt_step_state_t;

typedef struct {
    rt_step_state_t state;
    unsigned        action_idx;
    bool            waiting;         /* 正在 wait_time 倒计时 */
    uint32_t        wait_remaining;  /* wait_time 剩余 ms */
    uint32_t        done_elapsed;    /* done 未命中累计 ms */
    uint32_t        confirm_elapsed; /* done:signal 连续命中累计 ms */
    uint32_t        retry_used;      /* 已重试次数 */
    int             trig_prev;       /* signal 触发器上一拍电平 */
} rt_step_t;

typedef struct {
    rt_step_t *steps;
    unsigned   count;
} rt_lane_t;

typedef struct {
    double   position;
    bool     valid;
    int      sig_prev;
    unsigned signal_id;
    unsigned axis_id;
} marker_rt_t;

struct engine {
    engine_environment_t environment;
    engine_program_t    *prog;
    bool                 owns_prog;
    engine_run_state_t   state;

    int                   cur_phase;
    const engine_phase_t *cur_def;
    bool                  phase_entered;
    uint32_t              phase_elapsed;

    /* 运行态通道表：load 期按「全阶段最大规模」一次性分配后复用。
     * 早期实现在每次 enter_phase 里 calloc/free，长时间运行的设备反复切换阶段
     * 会持续制造堆碎片；预分配后 tick 路径不再有任何堆操作。 */
    rt_lane_t *lanes;          /* 容量 max_lane_count */
    unsigned   lane_count;     /* 当前阶段实际使用的通道数 */
    rt_step_t *step_pool;      /* 扁平步骤池，容量 max_lane_count * step_stride */
    unsigned   max_lane_count; /* 全阶段最大通道数 */
    unsigned   step_stride;    /* 每通道预留的步骤槽数（全阶段最大步骤数）*/

    marker_rt_t *markers;      /* 与 prog->markers 平行 */

    bool *ilk_active;          /* 与 prog->interlocks 平行 */
    int  *ilk_order;           /* 按 priority 升序的下标 */

    /** 本阶段持有的资源名（退出时 release，keep 除外） */
    unsigned held_resource_ids[ENGINE_HELD_CAP];
    unsigned held_count;
    sw_err_t last_error;

    bool frame_io_started;
    bool frame_profile_started;
    bool frame_variable_started;
};

/* -------------------------------------------------------------------------
 * IO / 执行机构访问
 * ------------------------------------------------------------------------- */
static void held_clear(engine_t *e);

static void engine_fail(engine_t *e, sw_err_t error)
{
    if ((e == NULL) || (error == SW_OK) || (e->state == ENGINE_STATE_FAULT)) {
        return;
    }
    e->last_error = error;
    e->state      = ENGINE_STATE_FAULT;
    (void)engine_actuator_halt_all(e->environment.actuator);
    held_clear(e);
}

static int sig_read(engine_t *e, unsigned signal_id)
{
    int      value = 0;
    sw_err_t error = engine_io_read_signal(e->environment.io, signal_id, &value);

    if (error != SW_OK) {
        engine_fail(e, error);
        return 0;
    }
    return value;
}

static void held_clear(engine_t *e)
{
    e->held_count = 0U;
}

static void held_mark(engine_t *e, unsigned resource_id)
{
    if (e == NULL) {
        return;
    }
    for (unsigned i = 0U; i < e->held_count; ++i) {
        if (e->held_resource_ids[i] == resource_id) {
            return;
        }
    }
    if (e->held_count >= ENGINE_HELD_CAP) {
        return;
    }
    e->held_resource_ids[e->held_count++] = resource_id;
}

static bool resource_in_keep(const engine_phase_t *ph, const char *resource)
{
    if ((ph == NULL) || (resource == NULL)) {
        return false;
    }
    for (unsigned i = 0U; i < ph->keep_count; ++i) {
        if (strcmp(ph->keep[i], resource) == 0) {
            return true;
        }
    }
    return false;
}

static bool intent_is_stop(const engine_intent_t *intent)
{
    return (intent != NULL) && (strcmp(intent->cmd, "stop") == 0);
}

/**
 * @brief 提交意图；track 为真且非 stop 时记入持有集
 */
static void intent_apply(engine_t *e, const engine_intent_t *intent, bool track)
{
    sw_err_t error;

    if ((intent == NULL) || !intent->resource_bound || (e->state == ENGINE_STATE_FAULT)) {
        return;
    }
    error = engine_actuator_apply(e->environment.actuator, intent->resource_id, intent);
    if (error != SW_OK) {
        engine_fail(e, error);
        return;
    }
    if (track && !intent_is_stop(intent)) {
        held_mark(e, intent->resource_id);
    }
}

static void resource_release(engine_t *e, unsigned resource_id)
{
    sw_err_t error;

    if ((e == NULL) || (e->state == ENGINE_STATE_FAULT)) {
        return;
    }
    error = engine_actuator_release(e->environment.actuator, resource_id);
    if (error != SW_OK) {
        engine_fail(e, error);
    }
}

/* -------------------------------------------------------------------------
 * 表达式求值环境
 * ------------------------------------------------------------------------- */
static bool engine_resolve_bound(void *ctx, unsigned token, double *out)
{
    engine_t            *e     = (engine_t *)ctx;
    unsigned             index = token_index(token);
    engine_axis_sample_t sample;
    sw_err_t             error;

    if ((e == NULL) || (out == NULL)) {
        return false;
    }
    switch (token_kind(token)) {
    case ENGINE_TOKEN_PARAM:
        if (index >= e->prog->param_count) {
            return false;
        }
        *out = e->prog->params[index].value;
        return true;
    case ENGINE_TOKEN_AXIS_POSITION:
    case ENGINE_TOKEN_AXIS_SPEED:
    case ENGINE_TOKEN_AXIS_VALID:
        error = engine_io_read_axis(e->environment.io, index, &sample);
        if (error != SW_OK) {
            engine_fail(e, error);
            return false;
        }
        if (token_kind(token) == ENGINE_TOKEN_AXIS_POSITION) {
            *out = sample.position;
        } else if (token_kind(token) == ENGINE_TOKEN_AXIS_SPEED) {
            *out = sample.speed;
        } else {
            *out = sample.valid ? 1.0 : 0.0;
        }
        return true;
    case ENGINE_TOKEN_MARKER_POSITION:
    case ENGINE_TOKEN_MARKER_VALID:
        if (index >= e->prog->marker_count) {
            return false;
        }
        *out = (token_kind(token) == ENGINE_TOKEN_MARKER_POSITION) ? e->markers[index].position
                                                                   : (e->markers[index].valid ? 1.0 : 0.0);
        return true;
    case ENGINE_TOKEN_PHASE_ELAPSED:
        *out = (double)e->phase_elapsed;
        return true;
    case ENGINE_TOKEN_PHASE_DIRECTION: {
        engine_direction_t direction = (e->cur_def != NULL) ? e->cur_def->direction : ENGINE_DIR_NONE;

        *out = (direction == ENGINE_DIR_FORWARD) ? 1.0 : ((direction == ENGINE_DIR_BACKWARD) ? -1.0 : 0.0);
        return true;
    }
    case ENGINE_TOKEN_VARIABLE:
        error = engine_variable_read(e->environment.variable, index, out);
        if (error != SW_OK) {
            engine_fail(e, error);
            return false;
        }
        return true;
    case ENGINE_TOKEN_SIGNAL:
        *out = (double)sig_read(e, index);
        return e->state != ENGINE_STATE_FAULT;
    default:
        return false;
    }
}

static bool engine_profile_height(void *ctx, double position, double default_value, double *out_height)
{
    engine_t *e     = (engine_t *)ctx;
    sw_err_t  error = engine_profile_height_at(e->environment.profile, position, default_value, out_height);

    if (error != SW_OK) {
        engine_fail(e, error);
        return false;
    }
    return true;
}

static bool engine_profile_zone(void *ctx, const char *zone, double position, bool default_value, bool *out_in_zone)
{
    engine_t *e     = (engine_t *)ctx;
    sw_err_t  error = engine_profile_in_zone(e->environment.profile, zone, position, default_value, out_in_zone);

    if (error != SW_OK) {
        engine_fail(e, error);
        return false;
    }
    return true;
}

static bool eval_bool(engine_t *e, const engine_expr_t *x, bool def)
{
    if (x == NULL) {
        return def;
    }
    engine_expr_env_t env = {
        .resolve           = NULL,
        .resolve_bound     = engine_resolve_bound,
        .profile_height_at = engine_profile_height,
        .profile_in_zone   = engine_profile_zone,
        .ctx               = e,
    };
    bool ok = false;
    bool v  = engine_expr_eval_bool(x, &env, &ok);
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
    int cur = sig_read(e, trig->signal_id);
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
/* 立即执行动作列表（仅 intent；wait_time 在此上下文忽略） */
static void run_actions_now(engine_t *e, const engine_action_t *acts, unsigned count, bool track)
{
    for (unsigned i = 0U; i < count; ++i) {
        if (acts[i].type == ENGINE_ACT_INTENT) {
            intent_apply(e, &acts[i].intent, track);
        }
    }
}

/* -------------------------------------------------------------------------
 * halt 处理
 * ------------------------------------------------------------------------- */
static void do_halt_all(engine_t *e)
{
    sw_err_t error = engine_actuator_halt_all(e->environment.actuator);

    if (error != SW_OK) {
        engine_fail(e, error);
        return;
    }
    held_clear(e);
    e->state = ENGINE_STATE_HALTED;
}

static void release_held_except_keep(engine_t *e)
{
    const engine_phase_t *ph = e->cur_def;

    for (unsigned i = 0U; i < e->held_count; ++i) {
        unsigned    resource_id = e->held_resource_ids[i];
        const char *res         = engine_actuator_catalog(e->environment.actuator)->resources[resource_id];
        if (!resource_in_keep(ph, res)) {
            resource_release(e, resource_id);
        }
    }
    held_clear(e);
}

static void run_phase_on_exit(engine_t *e)
{
    release_held_except_keep(e);
    if (e->cur_def != NULL) {
        run_actions_now(e, e->cur_def->on_exit, e->cur_def->on_exit_count, false);
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
/** 释放预分配的通道表与步骤池（仅在重新加载方案或销毁引擎时调用）*/
static void free_lanes(engine_t *e)
{
    free(e->lanes);
    e->lanes = NULL;
    free(e->step_pool);
    e->step_pool      = NULL;
    e->lane_count     = 0U;
    e->max_lane_count = 0U;
    e->step_stride    = 0U;
}

/**
 * @brief  按全阶段最大规模预分配通道表与步骤池
 * @note   在 load 期调用一次；此后 enter_phase 只做复位，不再触碰堆。
 */
static bool alloc_lanes(engine_t *e)
{
    const engine_program_t *p         = e->prog;
    unsigned                max_lanes = 0U;
    unsigned                max_steps = 0U;

    for (unsigned i = 0U; i < p->phase_count; ++i) {
        const engine_phase_t *ph = &p->phases[i];

        if (ph->lane_count > max_lanes) {
            max_lanes = ph->lane_count;
        }
        for (unsigned j = 0U; j < ph->lane_count; ++j) {
            if (ph->lanes[j].step_count > max_steps) {
                max_steps = ph->lanes[j].step_count;
            }
        }
    }

    /* 即使方案里没有任何通道/步骤也保留 1 个槽，避免零长度分配 */
    if (max_lanes == 0U) {
        max_lanes = 1U;
    }
    if (max_steps == 0U) {
        max_steps = 1U;
    }

    e->lanes = (rt_lane_t *)calloc(max_lanes, sizeof(rt_lane_t));
    if (e->lanes == NULL) {
        return false;
    }
    e->step_pool = (rt_step_t *)calloc((size_t)max_lanes * (size_t)max_steps, sizeof(rt_step_t));
    if (e->step_pool == NULL) {
        free(e->lanes);
        e->lanes = NULL;
        return false;
    }

    e->max_lane_count = max_lanes;
    e->step_stride    = max_steps;
    e->lane_count     = 0U;

    /* 各通道的步骤区间在池中固定切分，运行期不再变动 */
    for (unsigned i = 0U; i < max_lanes; ++i) {
        e->lanes[i].steps = &e->step_pool[(size_t)i * (size_t)max_steps];
        e->lanes[i].count = 0U;
    }

    return true;
}

/* 进入阶段 i：复位运行态、初始化步骤（不求 entry_guard、不执行 on_enter）
 * 本函数不做任何堆分配，容量已在 alloc_lanes 阶段按全阶段最大值预留。 */
static bool enter_phase(engine_t *e, int idx)
{
    e->cur_phase     = idx;
    e->cur_def       = &e->prog->phases[idx];
    e->phase_entered = false;
    e->phase_elapsed = 0U;
    held_clear(e);

    const engine_phase_t *ph = e->cur_def;

    /* 防御：容量取自同一 prog 的全阶段最大值，越界说明 prog 在 load 后被改写 */
    if ((e->lanes == NULL) || (ph->lane_count > e->max_lane_count)) {
        return false;
    }
    e->lane_count = ph->lane_count;

    for (unsigned i = 0U; i < ph->lane_count; ++i) {
        unsigned sc = ph->lanes[i].step_count;

        if (sc > e->step_stride) {
            return false;
        }
        e->lanes[i].count = sc;
        /* 复用槽位必须清零，否则会残留上一阶段的步骤状态 */
        (void)memset(e->lanes[i].steps, 0, (size_t)e->step_stride * sizeof(rt_step_t));
        for (unsigned j = 0U; j < sc; ++j) {
            const engine_step_t *sd = &ph->lanes[i].steps[j];
            rt_step_t           *rt = &e->lanes[i].steps[j];

            if (sd->type == ENGINE_STEP_CONTROL) {
                rt->state = RT_DONE;
                continue;
            }

            rt->state           = (sd->after_count > 0U) ? RT_WAIT_AFTER : RT_ARMED;
            rt->action_idx      = 0U;
            rt->waiting         = false;
            rt->wait_remaining  = 0U;
            rt->done_elapsed    = 0U;
            rt->confirm_elapsed = 0U;
            rt->retry_used      = 0U;
            rt->trig_prev       = (sd->trigger.type == ENGINE_TRIG_SIGNAL) ? sig_read(e, sd->trigger.signal_id) : 0;
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

/**
 * @brief 重发本步全部 intent（用于 done 超时重试）
 */
static void reapply_step_intents(engine_t *e, const engine_step_t *sd)
{
    unsigned i;

    if ((e == NULL) || (sd == NULL)) {
        return;
    }
    for (i = 0U; i < sd->action_count; ++i) {
        if (sd->actions[i].type == ENGINE_ACT_INTENT) {
            intent_apply(e, &sd->actions[i].intent, true);
        }
    }
}

/**
 * @brief done:signal 超时：未超 retry_max 则重发 intent，否则走 on_error
 */
static void handle_done_signal_timeout(engine_t *e, const engine_step_t *sd, rt_step_t *rt)
{
    if (rt->retry_used < sd->retry_max) {
        rt->retry_used++;
        rt->done_elapsed    = 0U;
        rt->confirm_elapsed = 0U;
        reapply_step_intents(e, sd);
        return;
    }
    apply_on_error(e, sd->on_error, rt);
}

/* -------------------------------------------------------------------------
 * control 型步骤：每拍按 active_while 应用/释放意图
 * ------------------------------------------------------------------------- */
static void control_step_tick(engine_t *e, const engine_step_t *sd)
{
    if (eval_bool(e, sd->active_while, false)) {
        intent_apply(e, &sd->intent, true);
    } else {
        resource_release(e, sd->intent.resource_id);
    }
}

/* -------------------------------------------------------------------------
 * 单步骤推进
 * ------------------------------------------------------------------------- */
static void step_tick(engine_t *e, const engine_step_t *sd, rt_step_t *rt, uint32_t dt)
{
    int cur_sig = (sd->trigger.type == ENGINE_TRIG_SIGNAL) ? sig_read(e, sd->trigger.signal_id) : 0;

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
                if (act->type == ENGINE_ACT_INTENT) {
                    intent_apply(e, &act->intent, true);
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
                int v = sig_read(e, sd->done.signal_id);

                if (v == sd->done.state) {
                    rt->confirm_elapsed += dt;
                    if ((sd->done.confirm_ms == 0U) || (rt->confirm_elapsed >= sd->done.confirm_ms)) {
                        rt->state = RT_DONE;
                    }
                } else {
                    rt->confirm_elapsed = 0U;
                    rt->done_elapsed += dt;
                    if ((sd->done.timeout_ms > 0U) && (rt->done_elapsed >= sd->done.timeout_ms)) {
                        handle_done_signal_timeout(e, sd, rt);
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
    for (unsigned i = 0U; i < e->prog->marker_count; ++i) {
        const engine_marker_t *mk = &e->prog->markers[i];
        marker_rt_t           *rt = &e->markers[i];
        int                    cur;

        if (mk->on_kind == ENGINE_MARKER_ON_CONDITION) {
            cur = eval_bool(e, mk->cond, false) ? 1 : 0;
        } else {
            cur = sig_read(e, rt->signal_id);
        }
        if (!rt->valid && edge_hit(mk->edge, rt->sig_prev, cur)) {
            engine_axis_sample_t sample;
            sw_err_t             error = engine_io_read_axis(e->environment.io, rt->axis_id, &sample);

            if (error == SW_OK) {
                rt->position = sample.position;
                rt->valid    = true;
            } else {
                engine_fail(e, error);
                return;
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
                    run_actions_now(e, il->actions, il->action_count, true);
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
        /* 方案跑完只停用通道，不释放预分配的池：
         * 引擎可被 engine_start 重新启动，池须保持可用。 */
        e->lane_count = 0U;
        e->cur_def    = NULL;
        e->cur_phase  = next;
        e->state      = ENGINE_STATE_DONE;
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
        run_actions_now(e, ph->on_enter, ph->on_enter_count, true);
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
engine_t *engine_create(const engine_environment_t *environment)
{
    if (engine_environment_validate(environment) != SW_OK) {
        return NULL;
    }
    engine_t *e = (engine_t *)calloc(1U, sizeof(engine_t));
    if (e != NULL) {
        e->environment = *environment;
        e->state       = ENGINE_STATE_IDLE;
        e->cur_phase   = -1;
        e->last_error  = SW_OK;
    }
    return e;
}

static bool begin_frame(engine_t *e)
{
    sw_err_t error;

    e->frame_io_started       = false;
    e->frame_profile_started  = false;
    e->frame_variable_started = false;

    if (e->environment.pre_tick != NULL) {
        error = e->environment.pre_tick(e->environment.pre_tick_ctx);
        if (error != SW_OK) {
            engine_fail(e, error);
            return false;
        }
    }
    error = engine_io_begin_tick(e->environment.io);
    if (error == SW_OK) {
        e->frame_io_started = true;
    }
    if (error == SW_OK) {
        error = engine_profile_begin_tick(e->environment.profile);
        if (error == SW_OK) {
            e->frame_profile_started = (e->environment.profile != NULL);
        }
    }
    if (error == SW_OK) {
        error = engine_variable_begin_tick(e->environment.variable);
        if (error == SW_OK) {
            e->frame_variable_started = (e->environment.variable != NULL);
        }
    }
    if (error != SW_OK) {
        engine_fail(e, error);
        return false;
    }
    return true;
}

static void end_frame(engine_t *e)
{
    if (e->frame_variable_started) {
        engine_variable_end_tick(e->environment.variable);
        e->frame_variable_started = false;
    }
    if (e->frame_profile_started) {
        engine_profile_end_tick(e->environment.profile);
        e->frame_profile_started = false;
    }
    if (e->frame_io_started) {
        engine_io_end_tick(e->environment.io);
        e->frame_io_started = false;
    }
}

static sw_err_t build_runtime_tables(engine_t *e)
{
    engine_program_t *p = e->prog;

    /* 通道表与步骤池按全阶段最大规模预分配，之后 tick 路径无堆操作 */
    if (!alloc_lanes(e)) {
        return SW_ERR_NOMEM;
    }

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
}

static bool copy_name_part(char *out, size_t out_size, const char *begin, const char *end)
{
    size_t length;

    if ((out == NULL) || (out_size == 0U) || (begin == NULL) || (end == NULL) || (end <= begin)) {
        return false;
    }
    length = (size_t)(end - begin);
    if (length >= out_size) {
        return false;
    }
    (void)memcpy(out, begin, length);
    out[length] = '\0';
    return true;
}

static bool bind_expression_name(void *ctx, const char *name, unsigned *out_token)
{
    engine_t *e = (engine_t *)ctx;

    if ((e == NULL) || (name == NULL) || (out_token == NULL)) {
        return false;
    }
    if (name[0] == '$') {
        for (unsigned i = 0U; i < e->prog->param_count; ++i) {
            if (strcmp(e->prog->params[i].name, name + 1) == 0) {
                *out_token = make_token(ENGINE_TOKEN_PARAM, i);
                return true;
            }
        }
        return false;
    }
    if (strncmp(name, "axes.", 5U) == 0) {
        const char *field = strrchr(name + 5, '.');
        char        axis_name[ENGINE_NAME_MAX];
        unsigned    axis_id;

        if ((field == NULL) || !copy_name_part(axis_name, sizeof(axis_name), name + 5, field)
            || (engine_io_find_axis(e->environment.io, axis_name, &axis_id) != SW_OK)) {
            return false;
        }
        if (strcmp(field + 1, "position") == 0) {
            *out_token = make_token(ENGINE_TOKEN_AXIS_POSITION, axis_id);
        } else if (strcmp(field + 1, "speed") == 0) {
            *out_token = make_token(ENGINE_TOKEN_AXIS_SPEED, axis_id);
        } else if (strcmp(field + 1, "valid") == 0) {
            *out_token = make_token(ENGINE_TOKEN_AXIS_VALID, axis_id);
        } else {
            return false;
        }
        return true;
    }
    if (strncmp(name, "markers.", 8U) == 0) {
        const char *field = strrchr(name + 8, '.');
        char        marker_name[ENGINE_NAME_MAX];

        if ((field == NULL) || !copy_name_part(marker_name, sizeof(marker_name), name + 8, field)) {
            return false;
        }
        for (unsigned i = 0U; i < e->prog->marker_count; ++i) {
            if (strcmp(e->prog->markers[i].id, marker_name) != 0) {
                continue;
            }
            if (strcmp(field + 1, "position") == 0) {
                *out_token = make_token(ENGINE_TOKEN_MARKER_POSITION, i);
            } else if (strcmp(field + 1, "valid") == 0) {
                *out_token = make_token(ENGINE_TOKEN_MARKER_VALID, i);
            } else {
                return false;
            }
            return true;
        }
        return false;
    }
    if (strcmp(name, "phase.elapsed_ms") == 0) {
        *out_token = make_token(ENGINE_TOKEN_PHASE_ELAPSED, 0U);
        return true;
    }
    if (strcmp(name, "phase.direction") == 0) {
        *out_token = make_token(ENGINE_TOKEN_PHASE_DIRECTION, 0U);
        return true;
    }
    {
        unsigned signal_id;

        if (engine_io_find_signal(e->environment.io, name, &signal_id) == SW_OK) {
            *out_token = make_token(ENGINE_TOKEN_SIGNAL, signal_id);
            return true;
        }
    }
    if (e->environment.variable != NULL) {
        unsigned variable_id;

        if (engine_variable_find(e->environment.variable, name, &variable_id) == SW_OK) {
            *out_token = make_token(ENGINE_TOKEN_VARIABLE, variable_id);
            return true;
        }
    }
    return false;
}

static bool bind_expression(engine_t *e, engine_expr_t *expression)
{
    return (expression == NULL) || engine_expr_bind(expression, bind_expression_name, e);
}

static bool bind_intent(engine_t *e, engine_intent_t *intent)
{
    const engine_actuator_catalog_t *catalog = engine_actuator_catalog(e->environment.actuator);

    if ((intent == NULL) || (catalog == NULL)) {
        return false;
    }
    for (unsigned i = 0U; i < catalog->resource_count; ++i) {
        if (strcmp(catalog->resources[i], intent->resource) == 0) {
            intent->resource_id    = i;
            intent->resource_bound = true;
            return true;
        }
    }
    return false;
}

static bool bind_actions(engine_t *e, engine_action_t *actions, unsigned count)
{
    for (unsigned i = 0U; i < count; ++i) {
        if ((actions[i].type == ENGINE_ACT_INTENT) && !bind_intent(e, &actions[i].intent)) {
            return false;
        }
    }
    return true;
}

static bool bind_program(engine_t *e)
{
    for (unsigned i = 0U; i < e->prog->marker_count; ++i) {
        engine_marker_t *marker = &e->prog->markers[i];

        if ((engine_io_find_axis(e->environment.io, marker->axis, &e->markers[i].axis_id) != SW_OK)
            || ((marker->on_kind == ENGINE_MARKER_ON_SIGNAL)
                && (engine_io_find_signal(e->environment.io, marker->signal, &e->markers[i].signal_id) != SW_OK))
            || !bind_expression(e, marker->cond)) {
            return false;
        }
    }
    for (unsigned i = 0U; i < e->prog->interlock_count; ++i) {
        engine_interlock_t *interlock = &e->prog->interlocks[i];

        if (!bind_expression(e, interlock->condition) || !bind_expression(e, interlock->reset_condition)
            || !bind_actions(e, interlock->actions, interlock->action_count)) {
            return false;
        }
    }
    for (unsigned i = 0U; i < e->prog->phase_count; ++i) {
        engine_phase_t *phase = &e->prog->phases[i];

        if (!bind_expression(e, phase->entry_guard) || !bind_expression(e, phase->exit_guard)
            || !bind_actions(e, phase->on_enter, phase->on_enter_count)
            || !bind_actions(e, phase->on_exit, phase->on_exit_count)) {
            return false;
        }
        for (unsigned lane_index = 0U; lane_index < phase->lane_count; ++lane_index) {
            engine_lane_t *lane = &phase->lanes[lane_index];

            for (unsigned step_index = 0U; step_index < lane->step_count; ++step_index) {
                engine_step_t *step = &lane->steps[step_index];

                if (step->type == ENGINE_STEP_CONTROL) {
                    if (!bind_expression(e, step->active_while) || !bind_intent(e, &step->intent)) {
                        return false;
                    }
                    continue;
                }
                if (!bind_expression(e, step->trigger.cond) || !bind_expression(e, step->guard)
                    || !bind_actions(e, step->actions, step->action_count)) {
                    return false;
                }
                if (step->trigger.type == ENGINE_TRIG_SIGNAL) {
                    if (engine_io_find_signal(e->environment.io, step->trigger.signal, &step->trigger.signal_id)
                        != SW_OK) {
                        return false;
                    }
                    step->trigger.signal_bound = true;
                }
                if (step->done.type == ENGINE_DONE_SIGNAL) {
                    if (engine_io_find_signal(e->environment.io, step->done.signal, &step->done.signal_id) != SW_OK) {
                        return false;
                    }
                    step->done.signal_bound = true;
                }
            }
        }
    }
    return true;
}

sw_err_t engine_load_program(engine_t *e, engine_program_t *prog)
{
    if ((e == NULL) || (prog == NULL)) {
        return SW_ERR_PARAM;
    }

    if (engine_program_validate(prog,
                                engine_io_catalog(e->environment.io),
                                engine_actuator_catalog(e->environment.actuator),
                                engine_variable_catalog(e->environment.variable),
                                NULL,
                                0U)
        != SW_OK) {
        engine_program_free(prog);
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
        /* 失败时必须解绑并置 HALTED：否则 prog 已挂上但运行态表可能只建了一半，
         * 调用方若忽略本函数返回值直接 engine_start，会在遍历 interlock_count
         * 时对 NULL 的 ilk_active 解引用。 */
        free_lanes(e);
        free_runtime_tables(e);
        engine_program_free(prog);
        e->prog      = NULL;
        e->owns_prog = false;
        e->state     = ENGINE_STATE_HALTED;
        return rc;
    }
    if (!bind_program(e)) {
        free_lanes(e);
        free_runtime_tables(e);
        engine_program_free(prog);
        e->prog      = NULL;
        e->owns_prog = false;
        e->state     = ENGINE_STATE_HALTED;
        return SW_ERR_PARAM;
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

    if (!begin_frame(e)) {
        end_frame(e);
        return e->last_error;
    }

    /* 复位标记与联锁 */
    for (unsigned i = 0U; i < e->prog->marker_count; ++i) {
        const engine_marker_t *mk = &e->prog->markers[i];
        e->markers[i].position    = 0.0;
        e->markers[i].valid       = false;
        if (mk->on_kind == ENGINE_MARKER_ON_CONDITION) {
            e->markers[i].sig_prev = eval_bool(e, mk->cond, false) ? 1 : 0;
        } else {
            e->markers[i].sig_prev = sig_read(e, e->markers[i].signal_id);
        }
    }
    for (unsigned i = 0U; i < e->prog->interlock_count; ++i) {
        e->ilk_active[i] = false;
    }

    e->state = ENGINE_STATE_RUNNING;
    if (!enter_phase(e, 0)) {
        if (e->state != ENGINE_STATE_FAULT) {
            e->state = ENGINE_STATE_HALTED;
        }
        end_frame(e);
        return (e->state == ENGINE_STATE_FAULT) ? e->last_error : SW_ERR_NOMEM;
    }
    end_frame(e);
    if (e->state == ENGINE_STATE_FAULT) {
        return e->last_error;
    }
    return SW_OK;
}

sw_err_t engine_tick(engine_t *e, uint32_t dt_ms)
{
    if ((e == NULL) || (e->prog == NULL)) {
        return SW_ERR_PARAM;
    }

    if (e->state == ENGINE_STATE_FAULT) {
        return e->last_error;
    }
    if (!begin_frame(e)) {
        end_frame(e);
        return e->last_error;
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
        end_frame(e);
        return (e->state == ENGINE_STATE_FAULT) ? e->last_error : SW_OK;
    }

    phase_tick(e, dt_ms);
    end_frame(e);
    return (e->state == ENGINE_STATE_FAULT) ? e->last_error : SW_OK;
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
                rt->state           = RT_ARMED;
                rt->action_idx      = 0U;
                rt->waiting         = false;
                rt->wait_remaining  = 0U;
                rt->done_elapsed    = 0U;
                rt->confirm_elapsed = 0U;
                /* retry_used 保留：recover 不扩大重试预算 */
            }
        }
    }

    if (e->cur_def != NULL) {
        run_actions_now(e, e->cur_def->on_enter, e->cur_def->on_enter_count, true);
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

sw_err_t engine_last_error(const engine_t *e)
{
    return (e != NULL) ? e->last_error : SW_ERR_PARAM;
}
