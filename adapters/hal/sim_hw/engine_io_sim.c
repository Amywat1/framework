/**
 * @file    engine_io_sim.c
 * @brief   引擎 IO 后端的仿真内存实现
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "adapters/hal/sim_hw/engine_io_sim.h"
#include "domain/engine/engine_io.h"

#include <string.h>

/* 容量上限（具名常量，避免魔法数字） */
#define ENGINE_IO_SIM_NAME_MAX     48U   /* 单个名称最大长度（含结尾 0） */
#define ENGINE_IO_SIM_SIGNAL_CAP   128U  /* DI 名条目上限 */
#define ENGINE_IO_SIM_OUTPUT_CAP   128U  /* DO 名条目上限 */
#define ENGINE_IO_SIM_AXIS_CAP     16U   /* 坐标轴条目上限 */

/* DI/DO 整数条目 */
typedef struct
{
    char name[ENGINE_IO_SIM_NAME_MAX];
    int  value;
} sim_int_entry_t;

/* 坐标轴条目 */
typedef struct
{
    char   name[ENGINE_IO_SIM_NAME_MAX];
    double pos;
    double speed;
    bool   valid;
} sim_axis_entry_t;

static sim_int_entry_t  s_signals[ENGINE_IO_SIM_SIGNAL_CAP];
static unsigned         s_signal_count;
static sim_int_entry_t  s_outputs[ENGINE_IO_SIM_OUTPUT_CAP];
static unsigned         s_output_count;
static sim_axis_entry_t s_axes[ENGINE_IO_SIM_AXIS_CAP];
static unsigned         s_axis_count;

/* -------------------------------------------------------------------------
 * 内部：按名查找整数条目，未找到时按需创建（容量满返回 NULL）
 * ------------------------------------------------------------------------- */
static sim_int_entry_t *int_find(sim_int_entry_t *table, unsigned *count,
                                 unsigned cap, const char *name, bool create)
{
    if (name == NULL)
    {
        return NULL;
    }
    for (unsigned i = 0U; i < *count; ++i)
    {
        if (strncmp(table[i].name, name, ENGINE_IO_SIM_NAME_MAX) == 0)
        {
            return &table[i];
        }
    }
    if (!create || (*count >= cap))
    {
        return NULL;
    }
    sim_int_entry_t *e = &table[*count];
    (void)strncpy(e->name, name, ENGINE_IO_SIM_NAME_MAX - 1U);
    e->name[ENGINE_IO_SIM_NAME_MAX - 1U] = '\0';
    e->value = 0;
    ++(*count);
    return e;
}

static sim_axis_entry_t *axis_find(const char *name, bool create)
{
    if (name == NULL)
    {
        return NULL;
    }
    for (unsigned i = 0U; i < s_axis_count; ++i)
    {
        if (strncmp(s_axes[i].name, name, ENGINE_IO_SIM_NAME_MAX) == 0)
        {
            return &s_axes[i];
        }
    }
    if (!create || (s_axis_count >= ENGINE_IO_SIM_AXIS_CAP))
    {
        return NULL;
    }
    sim_axis_entry_t *e = &s_axes[s_axis_count];
    (void)strncpy(e->name, name, ENGINE_IO_SIM_NAME_MAX - 1U);
    e->name[ENGINE_IO_SIM_NAME_MAX - 1U] = '\0';
    e->pos   = 0.0;
    e->speed = 0.0;
    e->valid = false;
    ++s_axis_count;
    return e;
}

/* -------------------------------------------------------------------------
 * engine_io_ops_t 实现
 * ------------------------------------------------------------------------- */
static int sim_read_signal(const char *name)
{
    const sim_int_entry_t *e =
        int_find(s_signals, &s_signal_count, ENGINE_IO_SIM_SIGNAL_CAP, name, false);
    return (e != NULL) ? e->value : 0;
}

static sw_err_t sim_read_axis(const char *name, double *out_pos,
                              double *out_speed, bool *out_valid)
{
    if ((out_pos == NULL) || (out_speed == NULL) || (out_valid == NULL))
    {
        return SW_ERR_PARAM;
    }
    const sim_axis_entry_t *e = axis_find(name, false);
    if (e == NULL)
    {
        *out_pos   = 0.0;
        *out_speed = 0.0;
        *out_valid = false;
        return SW_OK;
    }
    *out_pos   = e->pos;
    *out_speed = e->speed;
    *out_valid = e->valid;
    return SW_OK;
}

static void sim_write_output(const char *name, int value)
{
    sim_int_entry_t *e =
        int_find(s_outputs, &s_output_count, ENGINE_IO_SIM_OUTPUT_CAP, name, true);
    if (e != NULL)
    {
        e->value = value;
    }
}

static const engine_io_ops_t s_sim_ops = {
    .read_signal  = sim_read_signal,
    .read_axis    = sim_read_axis,
    .write_output = sim_write_output,
};

/* -------------------------------------------------------------------------
 * 公开接口
 * ------------------------------------------------------------------------- */
void engine_io_sim_register(void)
{
    engine_io_register(&s_sim_ops);
}

void engine_io_sim_reset(void)
{
    s_signal_count = 0U;
    s_output_count = 0U;
    s_axis_count   = 0U;
}

void engine_io_sim_set_signal(const char *name, int value)
{
    sim_int_entry_t *e =
        int_find(s_signals, &s_signal_count, ENGINE_IO_SIM_SIGNAL_CAP, name, true);
    if (e != NULL)
    {
        e->value = value;
    }
}

int engine_io_sim_get_signal(const char *name)
{
    return sim_read_signal(name);
}

void engine_io_sim_set_output(const char *name, int value)
{
    sim_write_output(name, value);
}

int engine_io_sim_get_output(const char *name)
{
    const sim_int_entry_t *e =
        int_find(s_outputs, &s_output_count, ENGINE_IO_SIM_OUTPUT_CAP, name, false);
    return (e != NULL) ? e->value : 0;
}

void engine_io_sim_set_axis(const char *name, double pos, double speed, bool valid)
{
    sim_axis_entry_t *e = axis_find(name, true);
    if (e != NULL)
    {
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
