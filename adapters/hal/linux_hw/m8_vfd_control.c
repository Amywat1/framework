/**
 * @file    m8_vfd_control.c
 * @brief   M8 VFD 统一控制实现
 * @author  胡望伟
 * @date    2026-06-01
 */

#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "adapters/hal/linux_hw/drv/drv_vfd.h"

sw_err_t m8_vfd_gantry_set_speed(int speed_ref)
{
    drv_vfd_t *vfd = m8_ctx_vfd_gantry();
    uint16_t   freq_hz;

    if (speed_ref > 0)
    {
        freq_hz = (uint16_t)speed_ref;
        return drv_vfd_run_fwd(vfd, freq_hz);
    }
    if (speed_ref < 0)
    {
        freq_hz = (uint16_t)(-speed_ref);
        return drv_vfd_run_rev(vfd, freq_hz);
    }
    return drv_vfd_stop(vfd);
}

sw_err_t m8_vfd_brush_set_speed(int speed_ref)
{
    drv_vfd_t *vfd = m8_ctx_vfd_brush();
    uint16_t   freq_hz;

    if (speed_ref > 0)
    {
        freq_hz = (uint16_t)speed_ref;
        return drv_vfd_run_fwd(vfd, freq_hz);
    }
    if (speed_ref < 0)
    {
        freq_hz = (uint16_t)(-speed_ref);
        return drv_vfd_run_rev(vfd, freq_hz);
    }
    return drv_vfd_stop(vfd);
}

sw_err_t m8_vfd_gantry_fault_reset(void)
{
    return drv_vfd_fault_reset(m8_ctx_vfd_gantry());
}

sw_err_t m8_vfd_brush_fault_reset(void)
{
    return drv_vfd_fault_reset(m8_ctx_vfd_brush());
}

bool m8_vfd_brush_is_running(void)
{
    return drv_vfd_get_state(m8_ctx_vfd_brush()) == DRV_VFD_STATE_FWD;
}
