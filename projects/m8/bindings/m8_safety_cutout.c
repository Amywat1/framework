/**
 * @file    m8_safety_cutout.c
 * @brief   M8 硬件急停快速切断强符号实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    safety_thread 热路径：仅缓冲 DO 写入 + VFD cutoff，不 flush 总线。
 */

#include "framework/ports/outbound/safety/safety_cutout_port.h"
#include "framework/common/sw_error.h"
#include "framework/domain/device_control/patterns/fluid_path.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "projects/m8/bindings/m8_motor_exec.h"
#include "projects/m8/config/m8_io_pins.h"

static sw_err_t cutout_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, val);
}

static const io_do_t s_cutout_do_pins[] = {
    M8_IO_DO_GANTRY_FWD,
    M8_IO_DO_GANTRY_REV,
    M8_IO_DO_GANTRY_HIGH_SPEED,
    M8_IO_DO_BRUSH_FWD,
    M8_IO_DO_BRUSH_REV,
    M8_IO_DO_BRUSH_HIGH_SPEED,
    M8_IO_DO_FAN_START,
    M8_IO_DO_WATER_CURTAIN,
    M8_IO_DO_WATER_TOP_FOAM,
    M8_IO_DO_WATER_TOP,
    M8_IO_DO_WATER_BUTTOM,
    M8_IO_DO_WATER_BUTTOM_FOAM,
    M8_IO_DO_WATER_PUMP,
    M8_IO_DO_SUBMERSIBLE_PUMP,
    M8_IO_DO_SIDE_BRUSH_ACT,
    M8_IO_DO_TOP_BRUSH_ACT,
    M8_IO_DO_TOP_BRUSH_UP,
    M8_IO_DO_TOP_BRUSH_DOWN,
    M8_IO_DO_ROD_EXTEND,
    M8_IO_DO_ROD_RETRACT,
};

void safety_cutout_execute(void)
{
    unsigned int i;

    for (i = 0U; i < (unsigned int)(sizeof(s_cutout_do_pins) / sizeof(s_cutout_do_pins[0])); i++)
    {
        (void)cutout_do_set(s_cutout_do_pins[i], false);
    }

    m8_motor_emergency_cutoff();
    fluid_path_emergency_off();
}
