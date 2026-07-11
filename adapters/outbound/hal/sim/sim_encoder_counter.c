/**
 * @file    sim_encoder_counter.c
 * @brief   仿真编码器累计计数源
 * @author  HUWANGWEI
 * @date    2026-04-14
 */

#include "adapters/outbound/hal/sim/sim_encoder_counter.h"
#include <stddef.h>
#include <stdatomic.h>

#define SIM_ENCODER_MOTOR_SLOTS    4

static atomic_uint s_counter[SIM_ENCODER_MOTOR_SLOTS];

void sim_encoder_counter_reset_all(void)
{
    for (int i = 0; i < SIM_ENCODER_MOTOR_SLOTS; i++)
    {
        atomic_store(&s_counter[i], 0U);
    }
}

void sim_encoder_counter_add_pulse(int id, int delta)
{
    uint32_t step;

    if ((id < 0) || (id >= SIM_ENCODER_MOTOR_SLOTS) || (delta == 0))
    {
        return;
    }

    /* 硬件计数器只累计脉冲数，方向由 motor.c 根据当前运动方向解释。 */
    step = (delta > 0) ? (uint32_t)delta : (uint32_t)(-delta);
    atomic_fetch_add(&s_counter[id], step);
}

sw_err_t sim_encoder_counter_read(int id, uint32_t *p_value)
{
    if ((id < 0) || (id >= SIM_ENCODER_MOTOR_SLOTS) || (p_value == NULL))
    {
        return SW_ERR_PARAM;
    }

    *p_value = atomic_load(&s_counter[id]);
    return SW_OK;
}

sw_err_t sim_encoder_counter_clear(int id)
{
    if ((id < 0) || (id >= SIM_ENCODER_MOTOR_SLOTS))
    {
        return SW_ERR_PARAM;
    }

    atomic_store(&s_counter[id], 0U);
    return SW_OK;
}
