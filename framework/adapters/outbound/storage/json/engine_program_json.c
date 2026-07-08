/**
 * @file    engine_program_json.c
 * @brief   JSON 配置加载器实现（cJSON → engine_program_t + schema 校验）
 * @author  huwangwei
 * @date    2026-06-26
 */

#include "framework/adapters/outbound/storage/json/engine_program_json.h"
#include "framework/ports/outbound/storage/engine_program_loader_port.h"
#include "framework/domain/wash/engine/engine_expr.h"
#include "framework/domain/wash/model/engine_program_validate.h"
#include "framework/domain/wash/engine/engine_io.h"
#include "third_party/cJSON/cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * 访问/转换辅助
 * ------------------------------------------------------------------------- */
static void jfail(char *err, unsigned errsz, const char *fmt, const char *arg)
{
    if ((err != NULL) && (errsz > 0U))
    {
        (void)snprintf(err, errsz, fmt, arg);
    }
}

static const char *jstr(const cJSON *o, const char *k)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    return ((it != NULL) && cJSON_IsString(it)) ? it->valuestring : NULL;
}

static bool jint(const cJSON *o, const char *k, int *out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    if ((it == NULL) || !cJSON_IsNumber(it)) { return false; }
    *out = it->valueint;
    return true;
}

static bool juint(const cJSON *o, const char *k, uint32_t *out)
{
    int v;
    if (!jint(o, k, &v) || (v < 0)) { return false; }
    *out = (uint32_t)v;
    return true;
}

static bool jdouble(const cJSON *o, const char *k, double *out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    if ((it == NULL) || !cJSON_IsNumber(it)) { return false; }
    *out = it->valuedouble;
    return true;
}

static void copy_name(char *dst, unsigned cap, const char *src)
{
    (void)snprintf(dst, cap, "%s", (src != NULL) ? src : "");
}

/* 编译表达式字段（必需） */
static engine_expr_t *build_expr(const cJSON *o, const char *key,
                                 char *err, unsigned errsz)
{
    const char *txt = jstr(o, key);
    if (txt == NULL)
    {
        jfail(err, errsz, "缺少表达式字段: %s", key);
        return NULL;
    }
    engine_expr_t *e = engine_expr_compile(txt);
    if (e == NULL)
    {
        jfail(err, errsz, "表达式编译失败: %s", key);
    }
    return e;
}

/* -------------------------------------------------------------------------
 * 动作列表
 * ------------------------------------------------------------------------- */
static engine_action_t *build_actions(const cJSON *arr, unsigned *out_count,
                                      char *err, unsigned errsz)
{
    *out_count = 0U;
    if (arr == NULL)
    {
        return NULL;  /* 缺省空列表 */
    }
    if (!cJSON_IsArray(arr))
    {
        jfail(err, errsz, "%s", "动作列表应为数组");
        return NULL;
    }
    int n = cJSON_GetArraySize(arr);
    if (n <= 0)
    {
        return NULL;
    }

    engine_action_t *acts = (engine_action_t *)calloc((size_t)n, sizeof(engine_action_t));
    if (acts == NULL) { jfail(err, errsz, "%s", "内存不足"); return NULL; }

    for (int i = 0; i < n; ++i)
    {
        const cJSON *item = cJSON_GetArrayItem(arr, i);
        const cJSON *iov  = cJSON_GetObjectItemCaseSensitive(item, "io_set");
        const cJSON *wtv  = cJSON_GetObjectItemCaseSensitive(item, "wait_time");

        if (iov != NULL)
        {
            const char *ch = jstr(iov, "channel");
            int          v = 0;
            if ((ch == NULL) || !jint(iov, "value", &v))
            {
                jfail(err, errsz, "%s", "io_set 缺少 channel/value");
                free(acts);
                return NULL;
            }
            acts[i].type  = ENGINE_ACT_IO_SET;
            acts[i].value = v;
            copy_name(acts[i].channel, ENGINE_NAME_MAX, ch);
        }
        else if (wtv != NULL)
        {
            uint32_t ms = 0U;
            if (!juint(wtv, "ms", &ms))
            {
                jfail(err, errsz, "%s", "wait_time 缺少 ms");
                free(acts);
                return NULL;
            }
            acts[i].type = ENGINE_ACT_WAIT_TIME;
            acts[i].ms   = ms;
        }
        else
        {
            jfail(err, errsz, "%s", "不支持的动作原语（仅 io_set/wait_time）");
            free(acts);
            return NULL;
        }
    }
    *out_count = (unsigned)n;
    return acts;
}

/* -------------------------------------------------------------------------
 * 步骤
 * ------------------------------------------------------------------------- */
static bool build_control_step(engine_step_t *st, const cJSON *node, char *err, unsigned errsz)
{
    const char *sid = jstr(node, "id");
    if (sid == NULL) { jfail(err, errsz, "%s", "步骤缺少 id"); return false; }
    copy_name(st->id, ENGINE_NAME_MAX, sid);
    st->type = ENGINE_STEP_CONTROL;

    st->active_while = build_expr(node, "active_while", err, errsz);
    if (st->active_while == NULL) { return false; }

    st->value_expr = build_expr(node, "value_expr", err, errsz);
    if (st->value_expr == NULL) { return false; }

    const char *out = jstr(node, "output");
    if (out == NULL) { jfail(err, errsz, "%s", "control 步骤缺少 output"); return false; }
    copy_name(st->output, ENGINE_NAME_MAX, out);

    st->on_error = ENGINE_ERR_HALT_PHASE;
    if (cJSON_GetObjectItemCaseSensitive(node, "on_error") != NULL)
    {
        if (!engine_error_strategy_from_str(jstr(node, "on_error"), &st->on_error))
        {
            jfail(err, errsz, "%s", "无效 on_error");
            return false;
        }
    }
    return true;
}

static bool build_event_step(engine_step_t *st, const cJSON *node, char *err, unsigned errsz)
{
    st->type = ENGINE_STEP_EVENT;

    const char *sid = jstr(node, "id");
    if (sid == NULL) { jfail(err, errsz, "%s", "步骤缺少 id"); return false; }
    copy_name(st->id, ENGINE_NAME_MAX, sid);

    /* 触发器 */
    const cJSON *trig = cJSON_GetObjectItemCaseSensitive(node, "trigger");
    if (trig == NULL) { jfail(err, errsz, "%s", "步骤缺少 trigger"); return false; }
    const char *tt = jstr(trig, "type");
    if (!engine_trigger_type_from_str(tt, &st->trigger.type))
    {
        jfail(err, errsz, "不支持的触发器类型: %s", (tt != NULL) ? tt : "(空)");
        return false;
    }
    if (st->trigger.type == ENGINE_TRIG_CONDITION)
    {
        st->trigger.cond = build_expr(trig, "expr", err, errsz);
        if (st->trigger.cond == NULL) { return false; }
    }
    else /* signal */
    {
        const char *sig = jstr(trig, "signal");
        if ((sig == NULL) || !engine_edge_from_str(jstr(trig, "edge"), &st->trigger.edge))
        {
            jfail(err, errsz, "%s", "signal 触发器缺少 signal/edge");
            return false;
        }
        copy_name(st->trigger.signal, ENGINE_NAME_MAX, sig);
    }

    /* 可选 guard */
    if (cJSON_GetObjectItemCaseSensitive(node, "guard") != NULL)
    {
        st->guard = build_expr(node, "guard", err, errsz);
        if (st->guard == NULL) { return false; }
    }

    /* 动作列表 */
    st->actions = build_actions(cJSON_GetObjectItemCaseSensitive(node, "actions"),
                                &st->action_count, err, errsz);
    if (err[0] != '\0') { return false; }

    /* done */
    const cJSON *done = cJSON_GetObjectItemCaseSensitive(node, "done");
    if (done == NULL) { jfail(err, errsz, "%s", "步骤缺少 done"); return false; }
    const char *dt = jstr(done, "type");
    if (!engine_done_type_from_str(dt, &st->done.type))
    {
        jfail(err, errsz, "不支持的 done 类型: %s", (dt != NULL) ? dt : "(空)");
        return false;
    }
    if (st->done.type == ENGINE_DONE_SIGNAL)
    {
        const char *sig = jstr(done, "signal");
        int          state = 0;
        if ((sig == NULL) || !jint(done, "state", &state))
        {
            jfail(err, errsz, "%s", "done signal 缺少 signal/state");
            return false;
        }
        copy_name(st->done.signal, ENGINE_NAME_MAX, sig);
        st->done.state = state;
    }
    if ((st->done.type == ENGINE_DONE_SIGNAL) || (st->done.type == ENGINE_DONE_TIMEOUT))
    {
        (void)juint(done, "timeout_ms", &st->done.timeout_ms);
    }

    /* on_error（默认 halt_phase） */
    st->on_error = ENGINE_ERR_HALT_PHASE;
    if (cJSON_GetObjectItemCaseSensitive(node, "on_error") != NULL)
    {
        if (!engine_error_strategy_from_str(jstr(node, "on_error"), &st->on_error))
        {
            jfail(err, errsz, "%s", "无效 on_error");
            return false;
        }
    }

    /* after */
    const cJSON *after = cJSON_GetObjectItemCaseSensitive(node, "after");
    if (after != NULL)
    {
        if (!cJSON_IsArray(after)) { jfail(err, errsz, "%s", "after 应为数组"); return false; }
        int an = cJSON_GetArraySize(after);
        if (an > (int)ENGINE_AFTER_MAX) { jfail(err, errsz, "%s", "after 依赖过多"); return false; }
        for (int i = 0; i < an; ++i)
        {
            const cJSON *it = cJSON_GetArrayItem(after, i);
            copy_name(st->after[i], ENGINE_NAME_MAX, cJSON_IsString(it) ? it->valuestring : "");
        }
        st->after_count = (unsigned)an;
    }

    return true;
}

static bool build_step(engine_step_t *st, const cJSON *node, char *err, unsigned errsz)
{
    const char *type = jstr(node, "type");
    engine_step_type_t stype;

    if (type == NULL) { jfail(err, errsz, "%s", "步骤缺少 type"); return false; }
    if (!engine_step_type_from_str(type, &stype))
    {
        jfail(err, errsz, "不支持的步骤类型: %s", type);
        return false;
    }

    if (stype == ENGINE_STEP_CONTROL)
    {
        return build_control_step(st, node, err, errsz);
    }
    return build_event_step(st, node, err, errsz);
}

/* -------------------------------------------------------------------------
 * 阶段
 * ------------------------------------------------------------------------- */
static bool build_phase(engine_phase_t *ph, const cJSON *node, char *err, unsigned errsz)
{
    const char *pid = jstr(node, "id");
    if (pid == NULL) { jfail(err, errsz, "%s", "阶段缺少 id"); return false; }
    copy_name(ph->id, ENGINE_NAME_MAX, pid);
    copy_name(ph->name, ENGINE_DISPLAY_MAX, jstr(node, "name"));

    ph->direction = ENGINE_DIR_NONE;
    if (cJSON_GetObjectItemCaseSensitive(node, "direction") != NULL)
    {
        if (!engine_direction_from_str(jstr(node, "direction"), &ph->direction))
        {
            jfail(err, errsz, "%s", "无效 direction");
            return false;
        }
    }

    ph->entry_guard = build_expr(node, "entry_guard", err, errsz);
    if (ph->entry_guard == NULL) { return false; }
    ph->exit_guard = build_expr(node, "exit_guard", err, errsz);
    if (ph->exit_guard == NULL) { return false; }

    if (!juint(node, "timeout_ms", &ph->timeout_ms))
    {
        jfail(err, errsz, "%s", "timeout_ms 非法");
        return false;
    }

    ph->on_timeout = ENGINE_ERR_HALT_PHASE;
    if (cJSON_GetObjectItemCaseSensitive(node, "on_timeout") != NULL)
    {
        if (!engine_error_strategy_from_str(jstr(node, "on_timeout"), &ph->on_timeout))
        {
            jfail(err, errsz, "%s", "无效 on_timeout");
            return false;
        }
    }

    ph->on_enter = build_actions(cJSON_GetObjectItemCaseSensitive(node, "on_enter"),
                                 &ph->on_enter_count, err, errsz);
    if (err[0] != '\0') { return false; }
    ph->on_exit = build_actions(cJSON_GetObjectItemCaseSensitive(node, "on_exit"),
                                &ph->on_exit_count, err, errsz);
    if (err[0] != '\0') { return false; }

    /* lanes */
    const cJSON *lanes = cJSON_GetObjectItemCaseSensitive(node, "lanes");
    if ((lanes == NULL) || !cJSON_IsArray(lanes) || (cJSON_GetArraySize(lanes) == 0))
    {
        jfail(err, errsz, "%s", "阶段缺少 lanes");
        return false;
    }
    int ln_cnt = cJSON_GetArraySize(lanes);
    ph->lanes = (engine_lane_t *)calloc((size_t)ln_cnt, sizeof(engine_lane_t));
    if (ph->lanes == NULL) { jfail(err, errsz, "%s", "内存不足"); return false; }
    ph->lane_count = (unsigned)ln_cnt;

    for (int i = 0; i < ln_cnt; ++i)
    {
        const cJSON   *ln   = cJSON_GetArrayItem(lanes, i);
        engine_lane_t *lane = &ph->lanes[i];
        copy_name(lane->id, ENGINE_NAME_MAX, jstr(ln, "id"));

        const cJSON *steps = cJSON_GetObjectItemCaseSensitive(ln, "steps");
        if ((steps == NULL) || !cJSON_IsArray(steps) || (cJSON_GetArraySize(steps) == 0))
        {
            jfail(err, errsz, "%s", "通道缺少 steps");
            return false;
        }
        int st_cnt = cJSON_GetArraySize(steps);
        lane->steps = (engine_step_t *)calloc((size_t)st_cnt, sizeof(engine_step_t));
        if (lane->steps == NULL) { jfail(err, errsz, "%s", "内存不足"); return false; }
        lane->step_count = (unsigned)st_cnt;

        for (int j = 0; j < st_cnt; ++j)
        {
            if (!build_step(&lane->steps[j], cJSON_GetArrayItem(steps, j), err, errsz))
            {
                return false;
            }
        }
    }

    return true;
}

/* -------------------------------------------------------------------------
 * 方案
 * ------------------------------------------------------------------------- */
static engine_program_t *build_program(const cJSON *root, char *err, unsigned errsz)
{
    const cJSON *prog = cJSON_GetObjectItemCaseSensitive(root, "program");
    if (prog == NULL) { jfail(err, errsz, "%s", "缺少顶层 program"); return NULL; }

    const char *ver = jstr(prog, "schema_version");
    if (ver == NULL) { jfail(err, errsz, "%s", "缺少 schema_version"); return NULL; }
    if (strcmp(ver, "1.0") != 0)
    {
        jfail(err, errsz, "不支持的 schema_version: %s", ver);
        return NULL;
    }

    engine_program_t *p = (engine_program_t *)calloc(1U, sizeof(engine_program_t));
    if (p == NULL) { jfail(err, errsz, "%s", "内存不足"); return NULL; }

    copy_name(p->schema_version, ENGINE_VERSION_MAX, ver);
    copy_name(p->id, ENGINE_NAME_MAX, jstr(prog, "id"));
    copy_name(p->name, ENGINE_DISPLAY_MAX, jstr(prog, "name"));

    /* params（对象，可选） */
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(prog, "params");
    if ((params != NULL) && cJSON_IsObject(params) && (cJSON_GetArraySize(params) > 0))
    {
        int n = cJSON_GetArraySize(params);
        p->params = (engine_param_t *)calloc((size_t)n, sizeof(engine_param_t));
        if (p->params == NULL) { jfail(err, errsz, "%s", "内存不足"); engine_program_free(p); return NULL; }
        p->param_count = (unsigned)n;
        int i = 0;
        const cJSON *child = NULL;
        cJSON_ArrayForEach(child, params)
        {
            copy_name(p->params[i].name, ENGINE_NAME_MAX, child->string);
            p->params[i].value = cJSON_IsNumber(child) ? child->valuedouble : 0.0;
            ++i;
        }
    }

    /* axes（对象，仅 physical） */
    const cJSON *axes = cJSON_GetObjectItemCaseSensitive(prog, "axes");
    if ((axes != NULL) && cJSON_IsObject(axes) && (cJSON_GetArraySize(axes) > 0))
    {
        int n = cJSON_GetArraySize(axes);
        p->axes = (engine_axis_t *)calloc((size_t)n, sizeof(engine_axis_t));
        if (p->axes == NULL) { jfail(err, errsz, "%s", "内存不足"); engine_program_free(p); return NULL; }
        p->axis_count = (unsigned)n;
        int i = 0;
        const cJSON *ax = NULL;
        cJSON_ArrayForEach(ax, axes)
        {
            const char *atype = jstr(ax, "type");
            if ((atype == NULL) || (strcmp(atype, "physical") != 0))
            {
                jfail(err, errsz, "仅支持 physical 轴: %s", ax->string);
                engine_program_free(p);
                return NULL;
            }
            copy_name(p->axes[i].id, ENGINE_NAME_MAX, ax->string);
            copy_name(p->axes[i].encoder, ENGINE_NAME_MAX, jstr(ax, "encoder"));
            p->axes[i].pulse_per_mm = 1.0;
            (void)jdouble(ax, "pulse_per_mm", &p->axes[i].pulse_per_mm);
            p->axes[i].direction = 1;
            (void)jint(ax, "direction", &p->axes[i].direction);
            ++i;
        }
    }

    /* markers（对象，仅 latch） */
    const cJSON *markers = cJSON_GetObjectItemCaseSensitive(prog, "markers");
    if ((markers != NULL) && cJSON_IsObject(markers) && (cJSON_GetArraySize(markers) > 0))
    {
        int n = cJSON_GetArraySize(markers);
        p->markers = (engine_marker_t *)calloc((size_t)n, sizeof(engine_marker_t));
        if (p->markers == NULL) { jfail(err, errsz, "%s", "内存不足"); engine_program_free(p); return NULL; }
        p->marker_count = (unsigned)n;
        int i = 0;
        const cJSON *mk = NULL;
        cJSON_ArrayForEach(mk, markers)
        {
            const char *mtype = jstr(mk, "type");
            if ((mtype == NULL) || (strcmp(mtype, "latch") != 0))
            {
                jfail(err, errsz, "仅支持 latch 标记: %s", mk->string);
                engine_program_free(p);
                return NULL;
            }
            const cJSON *on = cJSON_GetObjectItemCaseSensitive(mk, "on");
            const char  *sig = jstr(on, "signal");
            if ((sig == NULL) || !engine_edge_from_str(jstr(on, "edge"), &p->markers[i].edge))
            {
                jfail(err, errsz, "标记缺少 on.signal/edge: %s", mk->string);
                engine_program_free(p);
                return NULL;
            }
            copy_name(p->markers[i].id, ENGINE_NAME_MAX, mk->string);
            copy_name(p->markers[i].axis, ENGINE_NAME_MAX, jstr(mk, "axis"));
            copy_name(p->markers[i].signal, ENGINE_NAME_MAX, sig);
            ++i;
        }
    }

    /* interlocks（数组） */
    const cJSON *ilks = cJSON_GetObjectItemCaseSensitive(prog, "interlocks");
    if ((ilks != NULL) && cJSON_IsArray(ilks) && (cJSON_GetArraySize(ilks) > 0))
    {
        int n = cJSON_GetArraySize(ilks);
        p->interlocks = (engine_interlock_t *)calloc((size_t)n, sizeof(engine_interlock_t));
        if (p->interlocks == NULL) { jfail(err, errsz, "%s", "内存不足"); engine_program_free(p); return NULL; }
        p->interlock_count = (unsigned)n;
        for (int i = 0; i < n; ++i)
        {
            const cJSON        *il  = cJSON_GetArrayItem(ilks, i);
            engine_interlock_t *dst = &p->interlocks[i];
            copy_name(dst->id, ENGINE_NAME_MAX, jstr(il, "id"));

            dst->condition = build_expr(il, "condition", err, errsz);
            if (dst->condition == NULL) { engine_program_free(p); return NULL; }

            if (!engine_interlock_action_from_str(jstr(il, "action"), &dst->action))
            {
                jfail(err, errsz, "%s", "无效联锁 action");
                engine_program_free(p);
                return NULL;
            }
            if (dst->action == ENGINE_ILK_CUSTOM)
            {
                dst->actions = build_actions(cJSON_GetObjectItemCaseSensitive(il, "actions"),
                                             &dst->action_count, err, errsz);
                if (err[0] != '\0') { engine_program_free(p); return NULL; }
            }

            int prio = 0;
            (void)jint(il, "priority", &prio);
            dst->priority = prio;

            if (cJSON_GetObjectItemCaseSensitive(il, "reset_condition") != NULL)
            {
                dst->reset_condition = build_expr(il, "reset_condition", err, errsz);
                if (dst->reset_condition == NULL) { engine_program_free(p); return NULL; }
            }

            const cJSON *ar = cJSON_GetObjectItemCaseSensitive(il, "auto_reset");
            dst->auto_reset = ((ar != NULL) && cJSON_IsTrue(ar));
        }
    }

    /* phases（数组，必需） */
    const cJSON *phases = cJSON_GetObjectItemCaseSensitive(prog, "phases");
    if ((phases == NULL) || !cJSON_IsArray(phases) || (cJSON_GetArraySize(phases) == 0))
    {
        jfail(err, errsz, "%s", "缺少 phases");
        engine_program_free(p);
        return NULL;
    }
    int ph_cnt = cJSON_GetArraySize(phases);
    p->phases = (engine_phase_t *)calloc((size_t)ph_cnt, sizeof(engine_phase_t));
    if (p->phases == NULL) { jfail(err, errsz, "%s", "内存不足"); engine_program_free(p); return NULL; }
    p->phase_count = (unsigned)ph_cnt;
    for (int i = 0; i < ph_cnt; ++i)
    {
        if (!build_phase(&p->phases[i], cJSON_GetArrayItem(phases, i), err, errsz))
        {
            engine_program_free(p);
            return NULL;
        }
    }

    return p;
}

/* -------------------------------------------------------------------------
 * 公开入口
 * ------------------------------------------------------------------------- */
engine_program_t *engine_program_load_json_string(const char *json, char *err, unsigned errsz)
{
    char     local_err[160];
    char    *werr = (err != NULL && errsz > 0U) ? err : local_err;
    unsigned wsz  = (err != NULL && errsz > 0U) ? errsz : (unsigned)sizeof(local_err);
    werr[0] = '\0';

    if (json == NULL) { jfail(werr, wsz, "%s", "空输入"); return NULL; }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL)
    {
        jfail(werr, wsz, "%s", "JSON 解析失败");
        return NULL;
    }

    engine_program_t *prog = build_program(root, werr, wsz);
    cJSON_Delete(root);
    if (prog == NULL)
    {
        return NULL;
    }

    if (engine_program_validate(prog, engine_io_get_catalog(), werr, wsz) != SW_OK)
    {
        engine_program_free(prog);
        return NULL;
    }

    return prog;
}

engine_program_t *engine_program_load_json_file(const char *path, char *err, unsigned errsz)
{
    if (err != NULL && errsz > 0U) { err[0] = '\0'; }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) { jfail(err, errsz, "无法打开文件: %s", path); return NULL; }

    (void)fseek(fp, 0L, SEEK_END);
    long sz = ftell(fp);
    (void)fseek(fp, 0L, SEEK_SET);
    if (sz < 0) { jfail(err, errsz, "%s", "读取文件失败"); (void)fclose(fp); return NULL; }

    char *buf = (char *)malloc((size_t)sz + 1U);
    if (buf == NULL) { jfail(err, errsz, "%s", "内存不足"); (void)fclose(fp); return NULL; }
    size_t rd = fread(buf, 1U, (size_t)sz, fp);
    buf[rd] = '\0';
    (void)fclose(fp);

    engine_program_t *prog = engine_program_load_json_string(buf, err, errsz);
    free(buf);
    return prog;
}

static const engine_program_loader_ops_t s_json_loader_ops = {
    .load = engine_program_load_json_file,
};

void engine_program_json_register_loader(void)
{
    engine_program_loader_register(&s_json_loader_ops);
}
