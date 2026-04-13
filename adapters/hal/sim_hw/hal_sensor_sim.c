/**
 * @file    hal_sensor_sim.c
 * @brief   传感器 HAL 仿真实现（PC 调试 / 场景测试）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    默认状态：所有限位未触发，急停未激活，位置为 0，VFD 无故障。
 *          通过 hal_sensor_sim_set_*() 系列函数注入虚拟传感器状态。
 */

#include "adapters/hal/sim_hw/hal_sensor_sim.h"
#include "ports/hal/hal_sensor_port.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"
#include <stdatomic.h>

/* -------------------------------------------------------------------------
 * 虚拟传感器状态
 * ------------------------------------------------------------------------- */
static bool      s_fwd_limit    = false;
static bool      s_rev_limit    = false;
static bool      s_lift_top     = false;
static bool      s_lift_bottom  = false;
static bool      s_estop        = false;
static atomic_int s_gantry_pos  = 0;
static atomic_int s_encoder_pending = 0;

/* -------------------------------------------------------------------------
 * ops 实现
 * ------------------------------------------------------------------------- */
static bool sim_gantry_at_fwd_limit(void) { return s_fwd_limit; }
static bool sim_gantry_at_rev_limit(void) { return s_rev_limit; }
static bool sim_lift_at_top(void)         { return s_lift_top; }
static bool sim_lift_at_bottom(void)      { return s_lift_bottom; }
static bool sim_is_estop_active(void)     { return s_estop; }

static int32_t sim_get_gantry_pos(void)
{
    return (int32_t)atomic_load(&s_gantry_pos);
}

static void sim_reset_gantry_pos(void)
{
    atomic_store(&s_gantry_pos, 0);
    LOG_INFO("hal_sensor_sim: gantry pos reset");
}

static void sim_poll_input_events(void)
{
    static bool s_init             = false;
    static bool s_prev_estop       = false;
    static bool s_prev_fwd_limit   = false;
    static bool s_prev_rev_limit   = false;
    static bool s_prev_lift_top    = false;
    static bool s_prev_lift_bottom = false;

    if (!s_init)
    {
        s_prev_estop       = s_estop;
        s_prev_fwd_limit   = s_fwd_limit;
        s_prev_rev_limit   = s_rev_limit;
        s_prev_lift_top    = s_lift_top;
        s_prev_lift_bottom = s_lift_bottom;
        s_init             = true;
    }

    if (s_estop != s_prev_estop)
    {
        (void)event_publish(s_estop ? EVT_HW_ESTOP_ON : EVT_HW_ESTOP_OFF, 0U);
    }
    if (s_fwd_limit && !s_prev_fwd_limit)
    {
        (void)event_publish(EVT_HW_GANTRY_FWD_LIM, 0U);
    }
    if (s_rev_limit && !s_prev_rev_limit)
    {
        (void)event_publish(EVT_HW_GANTRY_REV_LIM, 0U);
    }
    if (s_lift_top && !s_prev_lift_top)
    {
        (void)event_publish(EVT_HW_LIFT_UP_LIM, 0U);
    }
    if (s_lift_bottom && !s_prev_lift_bottom)
    {
        (void)event_publish(EVT_HW_LIFT_DOWN_LIM, 0U);
    }

    s_prev_estop       = s_estop;
    s_prev_fwd_limit   = s_fwd_limit;
    s_prev_rev_limit   = s_rev_limit;
    s_prev_lift_top    = s_lift_top;
    s_prev_lift_bottom = s_lift_bottom;

    for (;;)
    {
        int pending = atomic_load(&s_encoder_pending);
        if (pending == 0)
        {
            break;
        }
        if (atomic_compare_exchange_weak(&s_encoder_pending, &pending, 0))
        {
            uint32_t param = (pending > 0) ? 1U : 0U;
            int count = (pending > 0) ? pending : -pending;
            for (int i = 0; i < count; i++)
            {
                (void)event_publish(EVT_HW_ENCODER_TICK, param);
            }
            break;
        }
    }
}

static sw_err_t sim_get_vfd_fault_code(hal_vfd_id_t vfd_id, uint16_t *p_code)
{
    (void)vfd_id;
    if (p_code == NULL) { return SW_ERR_PARAM; }
    *p_code = 0U; /* 仿真中无故障 */
    return SW_OK;
}

static const hal_sensor_ops_t s_ops = {
    .gantry_at_fwd_limit  = sim_gantry_at_fwd_limit,
    .gantry_at_rev_limit  = sim_gantry_at_rev_limit,
    .lift_at_top          = sim_lift_at_top,
    .lift_at_bottom       = sim_lift_at_bottom,
    .is_estop_active      = sim_is_estop_active,
    .get_gantry_pos       = sim_get_gantry_pos,
    .reset_gantry_pos     = sim_reset_gantry_pos,
    .poll_input_events    = sim_poll_input_events,
    .get_vfd_fault_code   = sim_get_vfd_fault_code,
};

void hal_sensor_sim_register(void)
{
    hal_sensor_register(&s_ops);
    LOG_INFO("hal_sensor_sim: registered");
}

/* -------------------------------------------------------------------------
 * 外部注入接口（供测试/工具使用）
 * ------------------------------------------------------------------------- */
void hal_sensor_sim_set_fwd_limit(bool v)   { s_fwd_limit   = v; }
void hal_sensor_sim_set_rev_limit(bool v)   { s_rev_limit   = v; }
void hal_sensor_sim_set_lift_top(bool v)    { s_lift_top    = v; }
void hal_sensor_sim_set_lift_bottom(bool v) { s_lift_bottom = v; }
void hal_sensor_sim_set_estop(bool v)       { s_estop       = v; }
void hal_sensor_sim_encoder_tick(int delta)
{
    atomic_fetch_add(&s_gantry_pos, delta);
    atomic_fetch_add(&s_encoder_pending, delta);
}
