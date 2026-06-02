/**
 * @file    hal_io_sim.c
 * @brief   数字 IO HAL 仿真实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "adapters/hal/sim_hw/hal_io_sim.h"
#include "ports/hal/hal_io_port.h"
#include "common/io_handle.h"
#include "common/log.h"

#include <string.h>

#define SIM_IO_BOARD_MAX    8U
#define SIM_IO_PIN_COUNT    32U

static bool s_do_state[SIM_IO_BOARD_MAX][SIM_IO_PIN_COUNT + 1U];
static bool s_di_state[SIM_IO_BOARD_MAX][SIM_IO_PIN_COUNT + 1U];

static bool sim_is_valid_di(io_di_t pin)
{
    uint16_t raw   = io_di_raw(pin);
    uint16_t board = io_handle_board(raw);
    uint16_t io    = io_handle_pin(raw);

    return (io_handle_kind(raw) == IO_KIND_DI)
        && (board > 0U)
        && (board < SIM_IO_BOARD_MAX)
        && (io > 0U)
        && (io <= SIM_IO_PIN_COUNT);
}

static bool sim_is_valid_do(io_do_t pin)
{
    uint16_t raw   = io_do_raw(pin);
    uint16_t board = io_handle_board(raw);
    uint16_t io    = io_handle_pin(raw);

    return (io_handle_kind(raw) == IO_KIND_DO)
        && (board > 0U)
        && (board < SIM_IO_BOARD_MAX)
        && (io > 0U)
        && (io <= SIM_IO_PIN_COUNT);
}

void hal_io_sim_set_di_level(io_di_t pin, bool level)
{
    uint16_t raw;
    uint16_t board;
    uint16_t io;

    if (!sim_is_valid_di(pin))
    {
        return;
    }

    raw   = io_di_raw(pin);
    board = io_handle_board(raw);
    io    = io_handle_pin(raw);
    s_di_state[board][io] = level;
}

static sw_err_t sim_do_set(io_do_t pin, bool val)
{
    uint16_t raw   = io_do_raw(pin);
    uint16_t board = io_handle_board(raw);
    uint16_t io    = io_handle_pin(raw);

    if (!sim_is_valid_do(pin))
    {
        return SW_ERR_PARAM;
    }

    s_do_state[board][io] = val;
    LOG_INFO("sim_io: DO(board=%u,pin=%u) = %d",
             (unsigned)board,
             (unsigned)io,
             (int)val);
    return SW_OK;
}

static bool sim_di_read(io_di_t pin)
{
    uint16_t raw;
    uint16_t board;
    uint16_t io;

    if (!sim_is_valid_di(pin))
    {
        return false;
    }

    raw   = io_di_raw(pin);
    board = io_handle_board(raw);
    io    = io_handle_pin(raw);
    return s_di_state[board][io];
}

static void sim_register_debug_input_cb(hal_io_debug_input_cb_t cb)
{
    (void)cb;
}

static void sim_register_board_status_cb(hal_io_board_status_cb_t cb)
{
    (void)cb;
}

static const hal_io_ops_t s_ops = {
    .do_set                   = sim_do_set,
    .di_read                  = sim_di_read,
    .register_debug_input_cb  = sim_register_debug_input_cb,
    .register_board_status_cb = sim_register_board_status_cb,
};

void hal_io_sim_register(void)
{
    memset(s_do_state, 0, sizeof(s_do_state));
    memset(s_di_state, 0, sizeof(s_di_state));
    hal_io_register(&s_ops);
    LOG_INFO("hal_io_sim: registered");
}
