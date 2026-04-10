/**
 * @file    hal_io_sim.c
 * @brief   数字 IO HAL 仿真实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_io_port.h"
#include "common/log.h"
#include <string.h>
#include <stdbool.h>

/* 简单位图记录 DO 状态（最多 256 路）*/
static bool s_do_state[256];

static sw_err_t sim_do_set(int io_id, bool val)
{
    if ((io_id < 0) || (io_id >= 256)) { return SW_ERR_PARAM; }
    s_do_state[io_id] = val;
    LOG_INFO("sim_io: DO[%d] = %d", io_id, (int)val);
    return SW_OK;
}

static bool sim_di_read(int io_id)
{
    (void)io_id;
    return false; /* 仿真中 DI 全为低 */
}

static void sim_register_input_cb(hal_io_input_cb_t cb)     { (void)cb; }
static void sim_register_board_status_cb(hal_io_board_status_cb_t cb) { (void)cb; }

static const hal_io_ops_t s_ops = {
    .do_set                  = sim_do_set,
    .di_read                 = sim_di_read,
    .register_input_cb       = sim_register_input_cb,
    .register_board_status_cb = sim_register_board_status_cb,
};

void hal_io_sim_register(void)
{
    memset(s_do_state, 0, sizeof(s_do_state));
    hal_io_register(&s_ops);
    LOG_INFO("hal_io_sim: registered");
}
