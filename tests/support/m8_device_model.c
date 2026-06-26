/**
 * @file    m8_device_model.c
 * @brief   M8 设备物理仿真模型实现
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "tests/support/m8_device_model.h"
#include "adapters/hal/sim_hw/engine_io_sim.h"

/* 物理参数（具名常量） */
#define DM_TRAVEL_LEN     1000.0   /* 龙门行程总长（脉冲） */
#define DM_GANTRY_STEP      10.0   /* 龙门每拍位移（脉冲） */
#define DM_LIFT_MAX        100.0   /* 升降行程 */
#define DM_LIFT_STEP        10.0   /* 升降每拍位移 */
#define DM_LOCK_MAX        100.0   /* 后轮锁止行程 */
#define DM_LOCK_STEP        10.0   /* 锁止每拍位移 */
#define DM_CAR_TAIL_POS    700.0   /* 车尾对应的龙门位置（脉冲） */

static double s_gantry_pos;  /* 龙门位置 0..DM_TRAVEL_LEN */
static double s_lift_pos;    /* 升降位置 0(下)..DM_LIFT_MAX(上) */
static double s_lock_pos;    /* 锁止位置 0..DM_LOCK_MAX(归位) */

static double clampd(double v, double lo, double hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

void m8_device_model_init(void)
{
    s_gantry_pos = 0.0;
    s_lift_pos   = 0.0;
    s_lock_pos   = 0.0;

    /* 初始安全信号：均无故障 */
    engine_io_sim_set_signal("ESTOP", 0);
    engine_io_sim_set_signal("BUMPER_LEFT", 0);
    engine_io_sim_set_signal("BUMPER_RIGHT", 0);
    engine_io_sim_set_signal("TOP_BRUSH_COLLISION", 0);
    engine_io_sim_set_signal("GANTRY_PAUSE_REQUEST", 0);

    /* 初始机构状态：龙门后限位、升降下限位、锁止未归位、未检测车尾 */
    engine_io_sim_set_signal("GANTRY_FWD_LIMIT", 0);
    engine_io_sim_set_signal("GANTRY_REV_LIMIT", 1);
    engine_io_sim_set_signal("LIFT_UP_LIMIT", 0);
    engine_io_sim_set_signal("LIFT_DOWN_LIMIT", 1);
    engine_io_sim_set_signal("REAR_LOCK_HOME", 0);
    engine_io_sim_set_signal("RADAR_CAR_TAIL", 0);

    engine_io_sim_set_axis("gantry", 0.0, 0.0, true);
}

void m8_device_model_tick(uint32_t dt_ms)
{
    (void)dt_ms;

    /* 龙门：FWD/REV 互斥推进 */
    int fwd = engine_io_sim_get_output("GANTRY_FWD");
    int rev = engine_io_sim_get_output("GANTRY_REV");
    if ((fwd != 0) && (rev == 0))
    {
        s_gantry_pos += DM_GANTRY_STEP;
    }
    else if ((rev != 0) && (fwd == 0))
    {
        s_gantry_pos -= DM_GANTRY_STEP;
    }
    s_gantry_pos = clampd(s_gantry_pos, 0.0, DM_TRAVEL_LEN);

    engine_io_sim_set_axis("gantry", s_gantry_pos, 0.0, true);
    engine_io_sim_set_signal("GANTRY_FWD_LIMIT", (s_gantry_pos >= DM_TRAVEL_LEN) ? 1 : 0);
    engine_io_sim_set_signal("GANTRY_REV_LIMIT", (s_gantry_pos <= 0.0) ? 1 : 0);
    /* 合成信号：龙门越过车尾位置时雷达置高 */
    engine_io_sim_set_signal("RADAR_CAR_TAIL", (s_gantry_pos >= DM_CAR_TAIL_POS) ? 1 : 0);

    /* 升降：UP/DOWN 互斥 */
    int lu = engine_io_sim_get_output("LIFTER_UP");
    int ld = engine_io_sim_get_output("LIFTER_DOWN");
    if ((lu != 0) && (ld == 0))
    {
        s_lift_pos += DM_LIFT_STEP;
    }
    else if ((ld != 0) && (lu == 0))
    {
        s_lift_pos -= DM_LIFT_STEP;
    }
    s_lift_pos = clampd(s_lift_pos, 0.0, DM_LIFT_MAX);
    engine_io_sim_set_signal("LIFT_UP_LIMIT", (s_lift_pos >= DM_LIFT_MAX) ? 1 : 0);
    engine_io_sim_set_signal("LIFT_DOWN_LIMIT", (s_lift_pos <= 0.0) ? 1 : 0);

    /* 后轮锁止：PUTTER_REV 驱动归位 */
    if (engine_io_sim_get_output("PUTTER_REV") != 0)
    {
        s_lock_pos += DM_LOCK_STEP;
    }
    s_lock_pos = clampd(s_lock_pos, 0.0, DM_LOCK_MAX);
    engine_io_sim_set_signal("REAR_LOCK_HOME", (s_lock_pos >= DM_LOCK_MAX) ? 1 : 0);
}
