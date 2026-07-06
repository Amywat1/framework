/**
 * @file    pulse_out.c
 * @brief   通用 DO 脉冲时序原语实现
 * @author  HUWANGWEI
 * @date    2026-07-04
 */

#include "framework/common/pulse_out.h"

#include "framework/common/time_util.h"

#include <stddef.h>

sw_err_t pulse_out_start(pulse_out_slot_t *slot, uint32_t pulse_ms, uint32_t now_ms)
{
    if ((slot == NULL) || (slot->set_level == NULL) || (pulse_ms == 0U))
    {
        return SW_ERR_PARAM;
    }

    slot->active    = true;
    slot->start_ms  = now_ms;
    slot->pulse_ms  = pulse_ms;
    return slot->set_level(slot->ctx, true);
}

void pulse_out_tick(pulse_out_slot_t *slot, uint32_t now_ms)
{
    if ((slot == NULL) || !slot->active)
    {
        return;
    }

    if (time_elapsed_ms(slot->start_ms, now_ms) >= slot->pulse_ms)
    {
        if (slot->set_level != NULL)
        {
            (void)slot->set_level(slot->ctx, false);
        }
        slot->active = false;
    }
}

bool pulse_out_is_active(const pulse_out_slot_t *slot)
{
    if (slot == NULL)
    {
        return false;
    }
    return slot->active;
}

void pulse_out_cancel(pulse_out_slot_t *slot)
{
    if (slot == NULL)
    {
        return;
    }

    if (slot->active && (slot->set_level != NULL))
    {
        (void)slot->set_level(slot->ctx, false);
    }
    slot->active = false;
}
