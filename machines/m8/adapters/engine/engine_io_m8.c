/**
 * @file    engine_io_m8.c
 * @brief   M8 机型引擎 IO 后端实现
 * @author  huwangwei
 * @date    2026-06-27
 *
 * @note    通道映射集中在三张静态表中：
 *
 *   s_signal_table  — DI 信号：名称 → 读取函数（NULL 表示 stub，返回 0）
 *   s_axis_table    — 坐标轴：名称 → 读取函数
 *   s_output_table  — DO 输出：名称 → 状态变量指针 + 分组刷新函数
 *                     （状态指针为 NULL 表示 stub，仅输出 LOG_WARN）
 *
 *   新增 / 删除 / 修改通道时，只需在对应表中增删一行，无需改动分发逻辑。
 *
 *   DO 分组刷新说明：
 *     apply_gantry()         — GANTRY_FWD / GANTRY_REV 互斥，两变量共享
 *     apply_brush()          — TOP_BRUSH_ROT 优先；SIDE 仅在 TOP 停止时生效
 *     apply_water_prewash()  — WATER_CURTAIN / TOP_FOAM / BUTTOM_FOAM 任一为 1 则开
 *     apply_water_highpres() — WATER_HIGHPRES_TOP / BOTTOM 任一为 1 则开
 *     TODO: water 驱动增加 water_valve_set() 后改为逐阀精确控制
 *
 *   标注 [stub] 的通道待实现真实驱动后，将 NULL 替换为真实实现即可。
 */

#include "machines/m8/adapters/engine/engine_io_m8.h"
#include "domain/engine/engine_io.h"
#include "domain/device/mechanism/gantry.h"
#include "domain/device/mechanism/brush.h"
#include "domain/device/mechanism/water.h"
#include "machines/m8/adapters/setup/m8_sensor.h"
#include "machines/m8/config/m8_signal_table.h"
#include "common/log.h"
#include "common/sw_types.h"

#include <string.h>
#include <stdint.h>

/* =========================================================================
 * DO 状态变量
 * 同组通道共享 apply 函数，各自持有独立状态变量，避免一方清零时误停另一方。
 * ========================================================================= */
static int s_gantry_fwd     = 0;
static int s_gantry_rev     = 0;
static int s_top_brush_rot  = 0;
static int s_side_brush_rot = 0;
static int s_water_curtain  = 0;
static int s_water_top_foam = 0;
static int s_water_btm_foam = 0;
static int s_water_hp_top   = 0;
static int s_water_hp_btm   = 0;

/* =========================================================================
 * DO 分组刷新函数
 * ========================================================================= */
static void apply_gantry(void)
{
    if (s_gantry_fwd > 0)
    {
        (void)gantry_move_fwd(1, NULL);
    }
    else if (s_gantry_rev > 0)
    {
        (void)gantry_move_rev(1, NULL);
    }
    else
    {
        (void)gantry_stop();
    }
}

static void apply_brush(void)
{
    if (s_top_brush_rot == 2)
    {
        /* 中速（pass4 高压冲洗段，挡位 2 = 45 Hz）*/
        (void)brush_start(BRUSH_TOP, 2);
    }
    else if (s_top_brush_rot == 1)
    {
        /* 正常洗车（挡位 1 = 35 Hz）*/
        (void)brush_start(BRUSH_TOP, 1);
    }
    else if (s_side_brush_rot > 0)
    {
        (void)brush_start(BRUSH_SIDE, 1);
    }
    else
    {
        (void)brush_stop();
    }
}

static void apply_water_prewash(void)
{
    if ((s_water_curtain > 0) || (s_water_top_foam > 0) || (s_water_btm_foam > 0))
    {
        (void)water_prewash_on();
    }
    else
    {
        (void)water_prewash_off();
    }
}

static void apply_water_highpres(void)
{
    if ((s_water_hp_top > 0) || (s_water_hp_btm > 0))
    {
        (void)water_highpres_on();
    }
    else
    {
        (void)water_highpres_off();
    }
}

/* =========================================================================
 * DI 信号表
 * fn == NULL → stub，分发函数直接返回 0，不打印 warn。
 * ========================================================================= */
typedef int (*signal_fn_t)(void);

typedef struct
{
    const char   *name;
    signal_fn_t   fn;
} signal_entry_t;

static int read_gantry_fwd_limit(void) { return m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM) ? 1 : 0; }
static int read_gantry_rev_limit(void) { return m8_signal_is_active(M8_SIG_GANTRY_REV_LIM) ? 1 : 0; }
static int read_lift_up_limit(void)    { return m8_signal_is_active(M8_SIG_LIFT_UP_LIM) ? 1 : 0; }
static int read_rear_lock_home(void)   { return m8_signal_is_active(M8_SIG_REAR_LOCK_HOME) ? 1 : 0; }

static const signal_entry_t s_signal_table[] = {
    { "GANTRY_FWD_LIMIT",     read_gantry_fwd_limit },
    { "GANTRY_REV_LIMIT",     read_gantry_rev_limit },
    { "LIFT_UP_LIMIT",        read_lift_up_limit    },
    { "REAR_LOCK_HOME",       read_rear_lock_home   },
    /* [stub] 待接入真实 DI 读取 API */
    { "ESTOP",                NULL },
    { "BUMPER_LEFT",          NULL },
    { "BUMPER_RIGHT",         NULL },
    { "TOP_BRUSH_COLLISION",  NULL },
    { "GANTRY_PAUSE_REQUEST", NULL },
    { "RADAR_CAR_TAIL",       NULL },
};

/* =========================================================================
 * 坐标轴表
 * ========================================================================= */
typedef sw_err_t (*axis_fn_t)(double *pos, double *speed, bool *valid);

typedef struct
{
    const char *name;
    axis_fn_t   fn;
} axis_entry_t;

static sw_err_t read_gantry_axis(double *pos, double *speed, bool *valid)
{
    *pos   = (double)gantry_position();
    *speed = 0.0;   /* TODO: 接入龙门速度反馈 */
    *valid = true;
    return SW_OK;
}

static const axis_entry_t s_axis_table[] = {
    { "gantry", read_gantry_axis },
};

/* =========================================================================
 * DO 输出表
 * p_state == NULL → stub，分发函数输出 LOG_WARN 后返回。
 * ========================================================================= */
typedef struct
{
    const char *name;
    int        *p_state;
    void      (*apply)(void);
} output_entry_t;

static const output_entry_t s_output_table[] = {
    { "GANTRY_FWD",            &s_gantry_fwd,       apply_gantry         },
    { "GANTRY_REV",            &s_gantry_rev,       apply_gantry         },
    { "TOP_BRUSH_ROT",         &s_top_brush_rot,    apply_brush          },
    { "SIDE_BRUSH_ROT",        &s_side_brush_rot,   apply_brush          },
    { "WATER_CURTAIN",         &s_water_curtain,    apply_water_prewash  },
    { "WATER_TOP_FOAM",        &s_water_top_foam,   apply_water_prewash  },
    { "WATER_BUTTOM_FOAM",     &s_water_btm_foam,   apply_water_prewash  },
    { "WATER_HIGHPRES_TOP",    &s_water_hp_top,     apply_water_highpres },
    { "WATER_HIGHPRES_BOTTOM", &s_water_hp_btm,     apply_water_highpres },
    /* [stub] 待实现对应驱动后替换为真实实现 */
    { "LIFTER_UP",             NULL, NULL },
    { "LIFTER_DOWN",           NULL, NULL },
    { "DRYER_RUN",             NULL, NULL },
    { "PUTTER_REV",            NULL, NULL },
    { "TOP_BRUSH_FOLLOW_EN",   NULL, NULL },
};

/* =========================================================================
 * 分发函数
 * ========================================================================= */
static int hal_read_signal(const char *name)
{
    for (size_t i = 0; i < ARRAY_SIZE(s_signal_table); i++)
    {
        if (strcmp(name, s_signal_table[i].name) == 0)
        {
            return s_signal_table[i].fn ? s_signal_table[i].fn() : 0;
        }
    }
    LOG_WARN("engine_io_m8: unknown signal [%s]", name);
    return 0;
}

static sw_err_t hal_read_axis(const char *name, double *out_pos,
                              double *out_speed, bool *out_valid)
{
    if ((out_pos == NULL) || (out_speed == NULL) || (out_valid == NULL))
    {
        return SW_ERR_PARAM;
    }
    for (size_t i = 0; i < ARRAY_SIZE(s_axis_table); i++)
    {
        if (strcmp(name, s_axis_table[i].name) == 0)
        {
            return s_axis_table[i].fn(out_pos, out_speed, out_valid);
        }
    }
    LOG_WARN("engine_io_m8: unknown axis [%s]", name);
    *out_pos   = 0.0;
    *out_speed = 0.0;
    *out_valid = false;
    return SW_ERR_PARAM;
}

static void hal_write_output(const char *name, int value)
{
    for (size_t i = 0; i < ARRAY_SIZE(s_output_table); i++)
    {
        if (strcmp(name, s_output_table[i].name) == 0)
        {
            if (s_output_table[i].p_state == NULL)
            {
                LOG_WARN("engine_io_m8: [stub] [%s]=%d (not connected)", name, value);
                return;
            }
            *s_output_table[i].p_state = value;
            s_output_table[i].apply();
            return;
        }
    }
    LOG_WARN("engine_io_m8: unknown DO channel [%s]=%d", name, value);
}

/* =========================================================================
 * 注册
 * ========================================================================= */
static const engine_io_ops_t s_ops = {
    .read_signal  = hal_read_signal,
    .read_axis    = hal_read_axis,
    .write_output = hal_write_output,
};

void engine_io_m8_register(void)
{
    engine_io_register(&s_ops);
}
