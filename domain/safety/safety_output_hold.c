/**
 * @file    safety_output_hold.c
 * @brief   机构输出抑制实现（原子锁存 + 可选 DI 采样）
 */
#include "domain/safety/safety_output_hold.h"

#include <stdatomic.h>
#include <stddef.h>

static bool (*s_di)(void);
static atomic_bool s_latch = false;

void safety_output_hold_bind_di(bool (*is_active)(void))
{
    s_di = is_active;
}

void safety_output_hold_request(void)
{
    atomic_store_explicit(&s_latch, true, memory_order_release);
}

bool safety_output_hold_is_active(void)
{
    bool (*di)(void) = s_di;

    if ((di != NULL) && di()) {
        atomic_store_explicit(&s_latch, true, memory_order_release);
        return true;
    }
    return atomic_load_explicit(&s_latch, memory_order_acquire);
}

sw_err_t safety_output_hold_release(void)
{
    bool (*di)(void) = s_di;

    if ((di != NULL) && di()) {
        return SW_ERR_STATE;
    }
    atomic_store_explicit(&s_latch, false, memory_order_release);
    return SW_OK;
}

void safety_output_hold_reset(void)
{
    s_di = NULL;
    atomic_store_explicit(&s_latch, false, memory_order_release);
}
