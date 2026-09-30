/**
 * @file    safety_energy_gen.c
 * @brief   切断能量代次实现
 */

#include "domain/safety/safety_energy_gen.h"

#include <stdatomic.h>

static atomic_uint s_energy_gen;

uint32_t safety_energy_gen_get(void)
{
    return (uint32_t)atomic_load_explicit(&s_energy_gen, memory_order_acquire);
}

void safety_energy_gen_bump(void)
{
    (void)atomic_fetch_add_explicit(&s_energy_gen, 1U, memory_order_acq_rel);
}

void safety_energy_gen_reset(void)
{
    atomic_store_explicit(&s_energy_gen, 0U, memory_order_relaxed);
}
