/**
 * @file    engine_io_m8.c
 * @brief   M8 机型引擎 IO 后端实现
 * @author  huwangwei
 * @date    2026-06-27
 *
 * @note    通道名与硬件 API 的映射关系：
 *
 *   DI 信号（read_signal）
 *     GANTRY_FWD_LIMIT   → gantry_at_fwd_limit()
 *     GANTRY_REV_LIMIT   → gantry_at_rev_limit()
 *     LIFT_UP_LIMIT      → m8_signal_is_active(M8_SIG_LIFT_UP_LIM)
 *     REAR_LOCK_HOME     → m8_signal_is_active(M8_SIG_REAR_LOCK_HOME)
 *     其余               → [stub] 返回 0，待接入真实 DI 读取 API
 *
 *   坐标轴（read_axis）
 *     "gantry"           → gantry_get_pos() / 速度固定 0
 *
 *   DO 通道（write_output）
 *     GANTRY_FWD / GANTRY_REV  → gantry_fwd/rev(svc_param "gantryFreqWash")
 *     TOP_BRUSH_ROT             → brush_start(TOP, freq)：1=低速 2=中速 0=停
 *     SIDE_BRUSH_ROT            → brush_start(SIDE, freq)：1=开 0=停
 *     WATER_CURTAIN/TOP_FOAM/BUTTOM_FOAM → water_prewash_on/off()（三通道任一为 1 开，全 0 关）
 *     WATER_HIGHPRES_TOP/BOTTOM        → water_highpres_on/off()（两通道任一为 1 开，全 0 关）
 *     注：以分组 API 近似实现，单阀粒度暂不支持；
 *         待 water 驱动增加 water_valve_set() 接口后精确对应。
 *     LIFTER_UP / LIFTER_DOWN   → [stub] 升降机驱动待实现
 *     DRYER_RUN                 → [stub] 吹风机驱动待实现
 *     PUTTER_REV                → [stub] 后轮锁推杆待实现
 *     TOP_BRUSH_FOLLOW_EN       → [stub] 随动控制待实现
 *
 *   标注 [stub] 的通道在实现真实驱动前输出 LOG_WARN，不操作硬件。
 */

#include "machines/m8/adapters/engine/engine_io_m8.h"
#include "domain/engine/engine_io.h"
#include "domain/device/unit/gantry.h"
#include "domain/device/unit/brush.h"
#include "domain/device/water.h"
#include "infrastructure/services/svc_param/svc_param.h"
#include "machines/m8/adapters/setup/m8_sensor.h"
#include "common/log.h"

#include <string.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 龙门方向状态（避免 FWD=0 时误停正在后退的龙门）
 * ------------------------------------------------------------------------- */
static int s_gantry_fwd = 0;
static int s_gantry_rev = 0;

static void apply_gantry(void)
{
    uint16_t freq = (uint16_t)svc_param_get_int("gantryFreqWash", 3000);
    if (s_gantry_fwd > 0)
    {
        (void)gantry_fwd(freq);
    }
    else if (s_gantry_rev > 0)
    {
        (void)gantry_rev(freq);
    }
    else
    {
        (void)gantry_stop();
    }
}

/* -------------------------------------------------------------------------
 * 刷子档位状态（TOP 和 SIDE 互斥，避免一方清零时意外停另一方）
 * ------------------------------------------------------------------------- */
static int s_top_brush_rot  = 0;
static int s_side_brush_rot = 0;

static void apply_brush(void)
{
    if (s_top_brush_rot == 2)
    {
        /* 中速（pass4 高压冲洗段）*/
        uint16_t freq = (uint16_t)svc_param_get_int("brushFreqTopMed", 5500);
        (void)brush_start(BRUSH_ID_TOP, freq);
    }
    else if (s_top_brush_rot == 1)
    {
        uint16_t freq = (uint16_t)svc_param_get_int("brushFreqTop", 4500);
        (void)brush_start(BRUSH_ID_TOP, freq);
    }
    else if (s_side_brush_rot > 0)
    {
        uint16_t freq = (uint16_t)svc_param_get_int("brushFreqSide", 4500);
        (void)brush_start(BRUSH_ID_SIDE, freq);
    }
    else
    {
        (void)brush_off();
    }
}

/* -------------------------------------------------------------------------
 * 水路通道状态（prewash 组 / highpres 组，分组 API 近似映射）
 * TODO: water 驱动增加 water_valve_set() 后改为逐阀精确控制
 * ------------------------------------------------------------------------- */
static int s_water_curtain  = 0;
static int s_water_top_foam = 0;
static int s_water_btm_foam = 0;
static int s_water_hp_top   = 0;
static int s_water_hp_btm   = 0;

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

/* -------------------------------------------------------------------------
 * read_signal 实现
 * ------------------------------------------------------------------------- */
static int hal_read_signal(const char *name)
{
    if (strcmp(name, "GANTRY_FWD_LIMIT") == 0)
    {
        return gantry_at_fwd_limit() ? 1 : 0;
    }
    if (strcmp(name, "GANTRY_REV_LIMIT") == 0)
    {
        return gantry_at_rev_limit() ? 1 : 0;
    }

    if (strcmp(name, "LIFT_UP_LIMIT") == 0)
    {
        return m8_signal_is_active(M8_SIG_LIFT_UP_LIM) ? 1 : 0;
    }
    if (strcmp(name, "REAR_LOCK_HOME") == 0)
    {
        return m8_signal_is_active(M8_SIG_REAR_LOCK_HOME) ? 1 : 0;
    }

    /* --- [stub] 以下信号待接入真实 DI 读取 API --- */
    if ((strcmp(name, "ESTOP")               == 0) ||
        (strcmp(name, "BUMPER_LEFT")         == 0) ||
        (strcmp(name, "BUMPER_RIGHT")        == 0) ||
        (strcmp(name, "TOP_BRUSH_COLLISION") == 0) ||
        (strcmp(name, "GANTRY_PAUSE_REQUEST") == 0) ||
        (strcmp(name, "RADAR_CAR_TAIL")      == 0))
    {
        return 0;
    }

    LOG_WARN("engine_io_m8: unknown signal [%s]", name);
    return 0;
}

/* -------------------------------------------------------------------------
 * read_axis 实现
 * ------------------------------------------------------------------------- */
static sw_err_t hal_read_axis(const char *name, double *out_pos,
                              double *out_speed, bool *out_valid)
{
    if ((out_pos == NULL) || (out_speed == NULL) || (out_valid == NULL))
    {
        return SW_ERR_PARAM;
    }

    if (strcmp(name, "gantry") == 0)
    {
        *out_pos   = (double)gantry_get_pos();
        *out_speed = 0.0;  /* TODO: 接入龙门速度反馈 */
        *out_valid = true;
        return SW_OK;
    }

    LOG_WARN("engine_io_m8: unknown axis [%s]", name);
    *out_pos   = 0.0;
    *out_speed = 0.0;
    *out_valid = false;
    return SW_ERR_PARAM;
}

/* -------------------------------------------------------------------------
 * write_output 实现
 * ------------------------------------------------------------------------- */
static void hal_write_output(const char *name, int value)
{
    /* 龙门 */
    if (strcmp(name, "GANTRY_FWD") == 0)
    {
        s_gantry_fwd = value;
        apply_gantry();
        return;
    }
    if (strcmp(name, "GANTRY_REV") == 0)
    {
        s_gantry_rev = value;
        apply_gantry();
        return;
    }

    /* 顶刷（多档位：0=停，1=低速，2=中速） */
    if (strcmp(name, "TOP_BRUSH_ROT") == 0)
    {
        s_top_brush_rot = value;
        apply_brush();
        return;
    }

    /* 侧刷（0=停，1=转） */
    if (strcmp(name, "SIDE_BRUSH_ROT") == 0)
    {
        s_side_brush_rot = value;
        apply_brush();
        return;
    }

    /* 水路：以分组 API 近似实现（单阀粒度待 water_valve_set() 接口就绪后精确化）
     * prewash 组：WATER_CURTAIN / WATER_TOP_FOAM / WATER_BUTTOM_FOAM
     * highpres 组：WATER_HIGHPRES_TOP / WATER_HIGHPRES_BOTTOM */
    if (strcmp(name, "WATER_CURTAIN") == 0)
    {
        s_water_curtain = value;
        apply_water_prewash();
        return;
    }
    if (strcmp(name, "WATER_TOP_FOAM") == 0)
    {
        s_water_top_foam = value;
        apply_water_prewash();
        return;
    }
    if (strcmp(name, "WATER_BUTTOM_FOAM") == 0)
    {
        s_water_btm_foam = value;
        apply_water_prewash();
        return;
    }
    if (strcmp(name, "WATER_HIGHPRES_TOP") == 0)
    {
        s_water_hp_top = value;
        apply_water_highpres();
        return;
    }
    if (strcmp(name, "WATER_HIGHPRES_BOTTOM") == 0)
    {
        s_water_hp_btm = value;
        apply_water_highpres();
        return;
    }

    /* --- [stub] 升降机：待实现 lifter 驱动 --- */
    if ((strcmp(name, "LIFTER_UP")   == 0) ||
        (strcmp(name, "LIFTER_DOWN") == 0))
    {
        LOG_WARN("engine_io_m8: [stub] lifter [%s]=%d (not connected)", name, value);
        return;
    }

    /* --- [stub] 吹风机：待实现 dryer 驱动 --- */
    if (strcmp(name, "DRYER_RUN") == 0)
    {
        LOG_WARN("engine_io_m8: [stub] dryer [%s]=%d (not connected)", name, value);
        return;
    }

    /* --- [stub] 后轮锁推杆：待实现 putter 驱动 --- */
    if (strcmp(name, "PUTTER_REV") == 0)
    {
        LOG_WARN("engine_io_m8: [stub] putter [%s]=%d (not connected)", name, value);
        return;
    }

    /* --- [stub] 顶刷随动使能：待接入随动控制器 --- */
    if (strcmp(name, "TOP_BRUSH_FOLLOW_EN") == 0)
    {
        LOG_WARN("engine_io_m8: [stub] follow_en [%s]=%d (not connected)", name, value);
        return;
    }

    LOG_WARN("engine_io_m8: unknown DO channel [%s]=%d", name, value);
}

/* -------------------------------------------------------------------------
 * 注册
 * ------------------------------------------------------------------------- */
static const engine_io_ops_t s_hal_ops = {
    .read_signal  = hal_read_signal,
    .read_axis    = hal_read_axis,
    .write_output = hal_write_output,
};

void engine_io_m8_register(void)
{
    engine_io_register(&s_hal_ops);
}
