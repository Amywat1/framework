/**
 * @file    engine_actuator_sim.c
 * @brief   引擎执行机构仿真后端实现
 */

#include "adapters/outbound/hal/sim/engine_actuator_sim.h"

#include "common/sw_error.h"
#include "domain/program_engine/engine/engine_actuator.h"

#include <stdio.h>
#include <string.h>

#define SIM_RES_MAX 16U

typedef struct {
    char name[ENGINE_NAME_MAX];
    char dir[ENGINE_NAME_MAX];
    int  active;
    int  gear;
} sim_res_t;

static sim_res_t s_res[SIM_RES_MAX];
static unsigned  s_res_count;

static const char *const s_resources[]
    = {"aout", "bout", "ctrl", "gantry", "lift", "rear_lock", "brush_top", "brush_side", "fan", "water", "roof_follow"};
static const char *const s_water_paths[] = {"curtain", "top_foam", "bottom_foam", "top_high", "bottom_high"};

static const engine_actuator_catalog_t s_catalog = {
    .resources        = s_resources,
    .resource_count   = (unsigned)(sizeof(s_resources) / sizeof(s_resources[0])),
    .water_paths      = s_water_paths,
    .water_path_count = (unsigned)(sizeof(s_water_paths) / sizeof(s_water_paths[0])),
};

static sim_res_t *find_or_add(const char *name)
{
    for (unsigned i = 0U; i < s_res_count; ++i) {
        if (strcmp(s_res[i].name, name) == 0) {
            return &s_res[i];
        }
    }
    if (s_res_count >= SIM_RES_MAX) {
        return NULL;
    }
    (void)snprintf(s_res[s_res_count].name, ENGINE_NAME_MAX, "%s", name);
    s_res[s_res_count].dir[0] = '\0';
    s_res[s_res_count].active = 0;
    s_res[s_res_count].gear   = 0;
    return &s_res[s_res_count++];
}

static sw_err_t sim_apply(const engine_intent_t *intent)
{
    sim_res_t *r;

    if ((intent == NULL) || (intent->resource[0] == '\0')) {
        return SW_ERR_PARAM;
    }
    r = find_or_add(intent->resource);
    if (r == NULL) {
        return SW_ERR_OVERFLOW;
    }
    if (strcmp(intent->cmd, "stop") == 0) {
        r->active = 0;
        r->gear   = 0;
        r->dir[0] = '\0';
        return SW_OK;
    }
    r->active = 1;
    r->gear   = (intent->gear > 0) ? intent->gear : 1;
    (void)snprintf(r->dir, sizeof(r->dir), "%s", intent->dir);
    return SW_OK;
}

static sw_err_t sim_release(const char *resource)
{
    engine_intent_t stop;

    (void)memset(&stop, 0, sizeof(stop));
    (void)snprintf(stop.resource, sizeof(stop.resource), "%s", resource);
    (void)snprintf(stop.cmd, sizeof(stop.cmd), "%s", "stop");
    return sim_apply(&stop);
}

static sw_err_t sim_halt_all(void)
{
    for (unsigned i = 0U; i < s_res_count; ++i) {
        s_res[i].active = 0;
        s_res[i].gear   = 0;
        s_res[i].dir[0] = '\0';
    }
    return SW_OK;
}

static bool sim_is_settled(const char *resource)
{
    unsigned i;

    if ((resource == NULL) || (resource[0] == '\0')) {
        return true;
    }
    for (i = 0U; i < s_res_count; ++i) {
        if (strcmp(s_res[i].name, resource) == 0) {
            return s_res[i].active == 0;
        }
    }
    return true;
}

static const engine_actuator_ops_t s_ops = {
    .apply      = sim_apply,
    .release    = sim_release,
    .halt_all   = sim_halt_all,
    .is_settled = sim_is_settled,
};

void engine_actuator_sim_register(void)
{
    static const engine_actuator_backend_t s_backend = {
        .ops     = &s_ops,
        .catalog = &s_catalog,
    };
    engine_actuator_register(&s_backend);
}

void engine_actuator_sim_reset(void)
{
    (void)memset(s_res, 0, sizeof(s_res));
    s_res_count = 0U;
}

int engine_actuator_sim_active(const char *resource)
{
    for (unsigned i = 0U; i < s_res_count; ++i) {
        if (strcmp(s_res[i].name, resource) == 0) {
            return s_res[i].active;
        }
    }
    return 0;
}

int engine_actuator_sim_gear(const char *resource)
{
    for (unsigned i = 0U; i < s_res_count; ++i) {
        if (strcmp(s_res[i].name, resource) == 0) {
            return s_res[i].gear;
        }
    }
    return 0;
}

const char *engine_actuator_sim_dir(const char *resource)
{
    for (unsigned i = 0U; i < s_res_count; ++i) {
        if (strcmp(s_res[i].name, resource) == 0) {
            return s_res[i].dir;
        }
    }
    return "";
}
