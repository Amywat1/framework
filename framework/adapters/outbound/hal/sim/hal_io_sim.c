/**
 * @file    hal_io_sim.c
 * @brief   数字 IO HAL 仿真实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/adapters/outbound/hal/sim/hal_io_sim.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/common/io_handle.h"
#include "framework/common/log.h"

#include <string.h>

#define SIM_IO_BOARD_MAX    8U
#define SIM_IO_PIN_COUNT    32U

static bool     s_do_state[SIM_IO_BOARD_MAX][SIM_IO_PIN_COUNT + 1U];
static bool     s_di_state[SIM_IO_BOARD_MAX][SIM_IO_PIN_COUNT + 1U];
static uint32_t s_pulse_counter[SIM_IO_BOARD_MAX][SIM_IO_PIN_COUNT + 1U];

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

static sw_err_t sim_io_init(void)
{
    memset(s_do_state, 0, sizeof(s_do_state));
    memset(s_di_state, 0, sizeof(s_di_state));
    memset(s_pulse_counter, 0, sizeof(s_pulse_counter));
    return SW_OK;
}

static sw_err_t sim_io_start(void)
{
    return SW_OK;
}

static void sim_register_panic_cb(hal_io_panic_cb_t cb)
{
    (void)cb;
}

static sw_err_t sim_flush_outputs_now(void)
{
    return SW_OK;
}

static bool sim_board_is_online(int board_id)
{
    (void)board_id;
    return true;
}

static sw_err_t sim_wait_boards_online(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return SW_OK;
}

static bool sim_try_parse_di(const char *name, io_di_t *out)
{
    (void)name;
    (void)out;
    return false;
}

static bool sim_try_parse_do(const char *name, io_do_t *out)
{
    (void)name;
    (void)out;
    return false;
}

static const char *sim_di_name(io_di_t pin)
{
    (void)pin;
    return NULL;
}

static const char *sim_do_name(io_do_t pin)
{
    (void)pin;
    return NULL;
}

static int sim_board_count(void)
{
    return 1;
}

static int sim_pulse_read(io_di_t pin)
{
    uint16_t board = io_handle_board(io_di_raw(pin));
    uint16_t p     = io_handle_pin(io_di_raw(pin));

    if (!sim_is_valid_di(pin))
    {
        return -1;
    }

    return (int)s_pulse_counter[board][p];
}

static sw_err_t sim_pulse_clear(io_di_t pin)
{
    uint16_t board = io_handle_board(io_di_raw(pin));
    uint16_t p     = io_handle_pin(io_di_raw(pin));

    if (!sim_is_valid_di(pin))
    {
        return SW_ERR_PARAM;
    }

    s_pulse_counter[board][p] = 0U;
    return SW_OK;
}

void hal_io_sim_set_pulse_counter(io_di_t pin, uint32_t value)
{
    uint16_t board = io_handle_board(io_di_raw(pin));
    uint16_t p     = io_handle_pin(io_di_raw(pin));

    if (!sim_is_valid_di(pin))
    {
        return;
    }

    s_pulse_counter[board][p] = value;
}

static sw_err_t sim_get_stats(int board_id, hal_io_stats_t *out)
{
    (void)board_id;
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));
    out->online = true;
    return SW_OK;
}

static const hal_io_ops_t s_ops = {
    .init                     = sim_io_init,
    .start                    = sim_io_start,
    .register_panic_cb        = sim_register_panic_cb,
    .flush_outputs_now        = sim_flush_outputs_now,
    .board_is_online          = sim_board_is_online,
    .wait_boards_online       = sim_wait_boards_online,
    .do_set                   = sim_do_set,
    .di_read                  = sim_di_read,
    .register_debug_input_cb  = sim_register_debug_input_cb,
    .register_board_status_cb = sim_register_board_status_cb,
    .try_parse_di             = sim_try_parse_di,
    .try_parse_do             = sim_try_parse_do,
    .di_name                  = sim_di_name,
    .do_name                  = sim_do_name,
    .board_count              = sim_board_count,
    .get_stats                = sim_get_stats,
    .pulse_read               = sim_pulse_read,
    .pulse_clear              = sim_pulse_clear,
};

void hal_io_sim_register(void)
{
    hal_io_register(&s_ops);
    (void)sim_io_init();
    LOG_INFO("hal_io_sim: registered");
}
