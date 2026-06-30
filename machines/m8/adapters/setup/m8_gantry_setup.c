/**
 * @file    m8_gantry_setup.c
 * @brief   M8 机型龙门执行器绑定（motor 层 API → gantry_actuator_ops_t）
 * @author  HUWANGWEI
 * @date    2026-06-30
 */

#include "machines/m8/adapters/setup/m8_gantry_setup.h"
#include "domain/device/unit/gantry.h"
#include "domain/device/actuator/motor/motor.h"
#include "machines/m8/config/m8_motor_table.h"

/* -------------------------------------------------------------------------
 * 运动完成回调桥接
 * motor_done_cb_t(int motor_id, sw_err_t, void*) → gantry_done_fn(sw_err_t)
 * ------------------------------------------------------------------------- */
static gantry_done_fn s_gantry_done_cb = NULL;

static void m8_gantry_motor_done(int motor_id, sw_err_t result, void *ctx)
{
    (void)motor_id;
    (void)ctx;
    if (s_gantry_done_cb != NULL)
    {
        s_gantry_done_cb(result);
    }
}

/* -------------------------------------------------------------------------
 * gantry_actuator_ops_t 实现
 * ------------------------------------------------------------------------- */
static sw_err_t m8_gantry_move_freq(int speed_ref)
{
    return motor_move(MOTOR_GANTRY, speed_ref);
}

static sw_err_t m8_gantry_move_gear(int8_t gear_dir)
{
    if (gear_dir > 0)
    {
        return motor_move_gear(MOTOR_GANTRY, (motor_gear_t)gear_dir, true);
    }
    if (gear_dir < 0)
    {
        return motor_move_gear(MOTOR_GANTRY, (motor_gear_t)(-gear_dir), false);
    }
    return motor_stop(MOTOR_GANTRY);
}

static sw_err_t m8_gantry_stop(void)
{
    return motor_stop(MOTOR_GANTRY);
}

static sw_err_t m8_gantry_set_done_cb(gantry_done_fn cb)
{
    s_gantry_done_cb = cb;
    return motor_set_done_cb(MOTOR_GANTRY,
                             (cb != NULL) ? m8_gantry_motor_done : NULL,
                             NULL);
}

static int32_t  m8_gantry_get_pos(void)       { return motor_get_pos(MOTOR_GANTRY); }
static sw_err_t m8_gantry_clear_pos(void)     { return motor_clear_encoder(MOTOR_GANTRY); }
static bool     m8_gantry_at_fwd_limit(void)  { return motor_at_fwd_limit(MOTOR_GANTRY); }
static bool     m8_gantry_at_rev_limit(void)  { return motor_at_rev_limit(MOTOR_GANTRY); }
static bool     m8_gantry_is_running(void)    { return motor_is_running(MOTOR_GANTRY); }
static bool     m8_gantry_is_fault(void)      { return motor_get_state(MOTOR_GANTRY) == MOTOR_STATE_FAULT; }
static uint16_t m8_gantry_get_current(void)   { return motor_get_current(MOTOR_GANTRY); }

static const gantry_actuator_ops_t s_gantry_ops = {
    .move_freq     = m8_gantry_move_freq,
    .move_gear     = m8_gantry_move_gear,
    .stop          = m8_gantry_stop,
    .set_done_cb   = m8_gantry_set_done_cb,
    .get_pos       = m8_gantry_get_pos,
    .clear_pos     = m8_gantry_clear_pos,
    .at_fwd_limit  = m8_gantry_at_fwd_limit,
    .at_rev_limit  = m8_gantry_at_rev_limit,
    .is_running    = m8_gantry_is_running,
    .is_fault      = m8_gantry_is_fault,
    .get_current   = m8_gantry_get_current,
};

/* -------------------------------------------------------------------------
 * 入口
 * ------------------------------------------------------------------------- */
sw_err_t m8_gantry_setup(void)
{
    return gantry_init(&s_gantry_ops);
}
