/**
 * @file    m8_manual_guard.c
 * @brief   M8 手动机构动作准入守卫实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/manual/m8_manual_guard.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/config/m8_signal_table.h"
#include "framework/common/log.h"

static bool is_estop_active(void)
{
    return m8_signal_is_active(M8_SIG_ESTOP);
}

static bool is_motion_device_state(dev_state_t st)
{
    return (st == DEV_STATE_IDLE) ||
           (st == DEV_STATE_STOP) ||
           (st == DEV_STATE_FAULT);
}

sw_err_t m8_manual_guard_allow_motion(void)
{
    dev_state_t st = dev_ctx_get_device_state();

    if (is_estop_active())
    {
        LOG_WARN("m8_manual_guard: motion rejected (estop active)");
        return SW_ERR_STATE;
    }

    if (!is_motion_device_state(st))
    {
        LOG_WARN("m8_manual_guard: motion rejected (device=%d)", (int)st);
        return SW_ERR_STATE;
    }

    return SW_OK;
}

sw_err_t m8_manual_guard_allow_stop(void)
{
    return SW_OK;
}
