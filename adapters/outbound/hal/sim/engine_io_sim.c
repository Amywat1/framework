/**
 * @file    engine_io_sim.c
 * @brief   引擎 IO 仿真后端实现（仅 DI / 坐标轴）
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "adapters/outbound/hal/sim/engine_io_sim.h"

#include "domain/ports/outbound/program_engine/engine_environment_provider.h"

#include <string.h>

#define ENGINE_IO_SIM_NAME_MAX   48U
#define ENGINE_IO_SIM_SIGNAL_CAP 128U
#define ENGINE_IO_SIM_AXIS_CAP   16U

typedef struct {
    char name[ENGINE_IO_SIM_NAME_MAX];
    int  value;
} sim_int_entry_t;

typedef struct {
    char   name[ENGINE_IO_SIM_NAME_MAX];
    double pos;
    double speed;
    bool   valid;
} sim_axis_entry_t;

static sim_int_entry_t  s_signals[ENGINE_IO_SIM_SIGNAL_CAP];
static unsigned         s_signal_count;
static sim_axis_entry_t s_axes[ENGINE_IO_SIM_AXIS_CAP];
static unsigned         s_axis_count;
static engine_io_t      s_handle;
static bool             s_bound;

static sim_int_entry_t *int_find(sim_int_entry_t *table, unsigned *count, unsigned cap, const char *name, bool create)
{
    if (name == NULL) {
        return NULL;
    }
    for (unsigned i = 0U; i < *count; ++i) {
        if (strncmp(table[i].name, name, ENGINE_IO_SIM_NAME_MAX) == 0) {
            return &table[i];
        }
    }
    if (!create || (*count >= cap)) {
        return NULL;
    }
    sim_int_entry_t *e = &table[*count];
    (void)strncpy(e->name, name, ENGINE_IO_SIM_NAME_MAX - 1U);
    e->name[ENGINE_IO_SIM_NAME_MAX - 1U] = '\0';
    e->value                             = 0;
    ++(*count);
    return e;
}

static sim_axis_entry_t *axis_find(const char *name, bool create)
{
    if (name == NULL) {
        return NULL;
    }
    for (unsigned i = 0U; i < s_axis_count; ++i) {
        if (strncmp(s_axes[i].name, name, ENGINE_IO_SIM_NAME_MAX) == 0) {
            return &s_axes[i];
        }
    }
    if (!create || (s_axis_count >= ENGINE_IO_SIM_AXIS_CAP)) {
        return NULL;
    }
    sim_axis_entry_t *e = &s_axes[s_axis_count];
    (void)strncpy(e->name, name, ENGINE_IO_SIM_NAME_MAX - 1U);
    e->name[ENGINE_IO_SIM_NAME_MAX - 1U] = '\0';
    e->pos                               = 0.0;
    e->speed                             = 0.0;
    e->valid                             = false;
    ++s_axis_count;
    return e;
}

static sw_err_t sim_read_signal(void *ctx, unsigned signal_id, int *out_value)
{
    const engine_io_catalog_t *catalog = (const engine_io_catalog_t *)ctx;
    const sim_int_entry_t     *entry;

    if ((out_value == NULL) || (signal_id >= catalog->signal_count)) {
        return SW_ERR_PARAM;
    }
    entry      = int_find(s_signals, &s_signal_count, ENGINE_IO_SIM_SIGNAL_CAP, catalog->signals[signal_id], false);
    *out_value = (entry != NULL) ? entry->value : 0;
    return SW_OK;
}

static sw_err_t sim_read_axis(void *ctx, unsigned axis_id, engine_axis_sample_t *out_sample)
{
    const engine_io_catalog_t *catalog = (const engine_io_catalog_t *)ctx;
    const sim_axis_entry_t    *entry;

    if ((out_sample == NULL) || (axis_id >= catalog->axis_count)) {
        return SW_ERR_PARAM;
    }
    entry = axis_find(catalog->axes[axis_id], false);
    if (entry == NULL) {
        out_sample->position = 0.0;
        out_sample->speed    = 0.0;
        out_sample->valid    = false;
        return SW_OK;
    }
    out_sample->position = entry->pos;
    out_sample->speed    = entry->speed;
    out_sample->valid    = entry->valid;
    return SW_OK;
}

static const engine_io_ops_t s_sim_ops = {
    .read_signal = sim_read_signal,
    .read_axis   = sim_read_axis,
};

static const char *const s_sim_signals[] = {
    "GANTRY_FWD_LIMIT",
    "GANTRY_REV_LIMIT",
    "LIFT_UP_LIMIT",
    "LIFT_DOWN_LIMIT",
    "REAR_LOCK_HOME",
    "ESTOP",
    "BUMPER_LEFT",
    "BUMPER_RIGHT",
    "TOP_BRUSH_COLLISION",
    "RADAR_CAR_TAIL",
    "EXIT",
    "EXIT0",
    "EXIT1",
    "SIG",
    "HP",
    "CA",
    "TAIL",
    "NEVER",
};

static const char *const s_sim_axes[] = {
    "gantry",
    "g",
};

static const engine_io_catalog_t s_sim_catalog = {
    .signals      = s_sim_signals,
    .signal_count = (unsigned)(sizeof(s_sim_signals) / sizeof(s_sim_signals[0])),
    .axes         = s_sim_axes,
    .axis_count   = (unsigned)(sizeof(s_sim_axes) / sizeof(s_sim_axes[0])),
};

engine_io_t *engine_io_sim_instance(void)
{
    if (!s_bound) {
        s_bound = engine_io_provider_bind(&s_handle, &s_sim_ops, (void *)&s_sim_catalog, &s_sim_catalog) == SW_OK;
    }
    return s_bound ? &s_handle : NULL;
}

void engine_io_sim_reset(void)
{
    s_signal_count = 0U;
    s_axis_count   = 0U;
}

void engine_io_sim_set_signal(const char *name, int value)
{
    sim_int_entry_t *e = int_find(s_signals, &s_signal_count, ENGINE_IO_SIM_SIGNAL_CAP, name, true);
    if (e != NULL) {
        e->value = value;
    }
}

void engine_io_sim_set_axis(const char *name, double pos, double speed, bool valid)
{
    sim_axis_entry_t *e = axis_find(name, true);
    if (e != NULL) {
        e->pos   = pos;
        e->speed = speed;
        e->valid = valid;
    }
}

double engine_io_sim_get_axis_pos(const char *name)
{
    const sim_axis_entry_t *e = axis_find(name, false);
    return (e != NULL) ? e->pos : 0.0;
}
