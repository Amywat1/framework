/**
 * @file    hal_motor_sim.c
 * @brief   通用电机 HAL 端口仿真实现
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "ports/hal/hal_motor_port.h"
#include "ports/hal/hal_sensor_port.h"
#include "config/machine/m8_motor_table.h"
#include "common/log.h"

static int s_speed_ref[MOTOR_ID_MAX];

static sw_err_t sim_motor_set_output(int id, int speed_ref)
{
    if ((id < 0) || (id >= MOTOR_ID_MAX))
    {
        return SW_ERR_PARAM;
    }

    s_speed_ref[id] = speed_ref;
    LOG_INFO("hal_motor_sim: id=%d speed_ref=%d", id, speed_ref);
    return SW_OK;
}

static bool sim_motor_at_fwd_limit(int id)
{
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();

    if ((id == MOTOR_GANTRY) && (ops != NULL) && (ops->gantry_at_fwd_limit != NULL))
    {
        return ops->gantry_at_fwd_limit();
    }
    return false;
}

static bool sim_motor_at_rev_limit(int id)
{
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();

    if ((id == MOTOR_GANTRY) && (ops != NULL) && (ops->gantry_at_rev_limit != NULL))
    {
        return ops->gantry_at_rev_limit();
    }
    return false;
}

static int32_t sim_motor_get_pos(int id)
{
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();

    if ((id == MOTOR_GANTRY) && (ops != NULL) && (ops->get_gantry_pos != NULL))
    {
        return ops->get_gantry_pos();
    }
    return -1;
}

static sw_err_t sim_motor_clear_pos(int id)
{
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();

    if ((id != MOTOR_GANTRY) || (ops == NULL) || (ops->reset_gantry_pos == NULL))
    {
        return SW_ERR_PARAM;
    }

    ops->reset_gantry_pos();
    return SW_OK;
}

static sw_err_t sim_motor_read_hw_pulse(int id, uint32_t *p_value)
{
    (void)id;
    (void)p_value;
    return SW_ERR_PARAM;
}

static sw_err_t sim_motor_clear_hw_pulse(int id)
{
    (void)id;
    return SW_ERR_PARAM;
}

static sw_err_t sim_motor_read_current(int id, uint16_t *p_current)
{
    (void)id;
    (void)p_current;
    return SW_ERR_PARAM;
}

static sw_err_t sim_motor_read_status(int id, uint16_t *p_status)
{
    (void)id;
    (void)p_status;
    return SW_ERR_PARAM;
}

static const hal_motor_ops_t s_ops = {
    .set_output      = sim_motor_set_output,
    .at_fwd_limit    = sim_motor_at_fwd_limit,
    .at_rev_limit    = sim_motor_at_rev_limit,
    .get_pos         = sim_motor_get_pos,
    .clear_pos       = sim_motor_clear_pos,
    .read_hw_pulse   = sim_motor_read_hw_pulse,
    .clear_hw_pulse  = sim_motor_clear_hw_pulse,
    .read_current    = sim_motor_read_current,
    .read_status     = sim_motor_read_status,
};

void hal_motor_sim_register(void)
{
    for (int i = 0; i < MOTOR_ID_MAX; i++)
    {
        s_speed_ref[i] = 0;
    }
    hal_motor_register(&s_ops);
    LOG_INFO("hal_motor_sim: registered");
}
