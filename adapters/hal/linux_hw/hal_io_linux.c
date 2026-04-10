/**
 * @file    hal_io_linux.c
 * @brief   数字 IO HAL 端口 — Linux 真机实现（薄封装 drv_io）
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_io_port.h"
#include "driver/drv_io.h"

static sw_err_t m8_do_set(int io_id, bool val)
{
    return drv_io_do_set((drv_io_do_t)io_id, val);
}

static bool m8_di_read(int io_id)
{
    return drv_io_di_read((drv_io_di_t)io_id);
}

static void m8_register_input_cb(hal_io_input_cb_t cb)
{
    drv_io_register_input_cb(cb);
}

static void m8_register_board_status_cb(hal_io_board_status_cb_t cb)
{
    drv_io_register_board_error_cb(cb);
}

static const hal_io_ops_t s_ops = {
    .do_set                 = m8_do_set,
    .di_read                = m8_di_read,
    .register_input_cb      = m8_register_input_cb,
    .register_board_status_cb = m8_register_board_status_cb,
};

void hal_io_linux_register(void)
{
    hal_io_register(&s_ops);
}
