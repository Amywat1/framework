/**
 * @file    m8_hw_estop.c
 * @brief   M8 硬件急停原始 DI 读取（safety_thread 快速通道）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    绕过 hal_sensor 滤波防抖，直接读 DI 原始电平并按极性换算。
 */

#include "framework/ports/outbound/safety/hw_estop_port.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "projects/m8/config/m8_io_pins.h"
#include "projects/m8/config/m8_signal_table.h"

bool hw_estop_port_is_active(void)
{
    const hal_io_ops_t *io = hal_io_get_ops();
    bool                raw;

    if ((io == NULL) || (io->di_read == NULL))
    {
        return false;
    }

    if (io_di_raw(M8_IO_DI_ESTOP) == IO_HANDLE_NULL)
    {
        return false;
    }

    raw = io->di_read(M8_IO_DI_ESTOP);

    if (m8_signal_table[M8_SIG_ESTOP].active_low)
    {
        return !raw;
    }

    return raw;
}
