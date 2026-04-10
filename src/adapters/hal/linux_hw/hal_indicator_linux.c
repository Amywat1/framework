/**
 * @file    hal_indicator_linux.c
 * @brief   入口指示灯与挡杆 HAL 端口 — Linux 真机实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_indicator_port.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "driver/drv_io.h"

static sw_err_t m8_entry_light_set(hal_light_state_t state)
{
    /* 先全灭再按状态点亮，防止多路同时亮 */
    (void)drv_io_do_set(M8_DO_ENTRY_GREEN1, false);
    (void)drv_io_do_set(M8_DO_ENTRY_GREEN2, false);
    (void)drv_io_do_set(M8_DO_ENTRY_RED,    false);
    (void)drv_io_do_set(M8_DO_ENTRY_YELLOW, false);

    switch (state)
    {
        case HAL_LIGHT_GREEN:
            (void)drv_io_do_set(M8_DO_ENTRY_GREEN1, true);
            (void)drv_io_do_set(M8_DO_ENTRY_GREEN2, true);
            break;
        case HAL_LIGHT_RED:
            (void)drv_io_do_set(M8_DO_ENTRY_RED, true);
            break;
        case HAL_LIGHT_YELLOW:
            (void)drv_io_do_set(M8_DO_ENTRY_YELLOW, true);
            break;
        case HAL_LIGHT_OFF:
        default:
            break;
    }
    return SW_OK;
}

static sw_err_t m8_rod_open(void)
{
    (void)drv_io_do_set(M8_DO_ROD_EXTEND,  false);
    (void)drv_io_do_set(M8_DO_ROD_RETRACT, true);
    return SW_OK;
}

static sw_err_t m8_rod_close(void)
{
    (void)drv_io_do_set(M8_DO_ROD_RETRACT, false);
    (void)drv_io_do_set(M8_DO_ROD_EXTEND,  true);
    return SW_OK;
}

static const hal_indicator_ops_t s_ops = {
    .entry_light_set = m8_entry_light_set,
    .rod_open        = m8_rod_open,
    .rod_close       = m8_rod_close,
};

void hal_indicator_linux_register(void)
{
    hal_indicator_register(&s_ops);
}
