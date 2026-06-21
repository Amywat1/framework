/**
 * @file    hal_io_linux.c
 * @brief   数字 IO HAL 端口 Linux 真机实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "ports/hal/hal_io_port.h"
#include "adapters/hal/linux_hw/drv/drv_io.h"

static sw_err_t io_init(void)
{
    return drv_io_init();
}

static sw_err_t io_start(void)
{
    return drv_io_start();
}

static void register_panic_cb(hal_io_panic_cb_t cb)
{
    drv_io_register_panic_cb(cb);
}

static sw_err_t flush_outputs_now(void)
{
    return drv_io_flush_outputs_now();
}

static bool board_is_online(int board_id)
{
    return drv_io_board_is_online(board_id);
}

static sw_err_t wait_boards_online(uint32_t timeout_ms)
{
    return drv_io_wait_boards_online(timeout_ms);
}

static sw_err_t do_set(io_do_t pin, bool val)
{
    return drv_io_do_set((drv_io_do_t)pin, val);
}

static bool di_read(io_di_t pin)
{
    return drv_io_di_read((drv_io_di_t)pin);
}

static void register_debug_input_cb(hal_io_debug_input_cb_t cb)
{
    drv_io_register_debug_input_cb(cb);
}

static void register_board_status_cb(hal_io_board_status_cb_t cb)
{
    drv_io_register_board_error_cb(cb);
}

static bool try_parse_di(const char *name, io_di_t *out)
{
    return drv_io_try_parse_di(name, (drv_io_di_t *)out);
}

static bool try_parse_do(const char *name, io_do_t *out)
{
    return drv_io_try_parse_do(name, (drv_io_do_t *)out);
}

static const char *di_name(io_di_t pin)
{
    return drv_io_di_name((drv_io_di_t)pin);
}

static const char *do_name(io_do_t pin)
{
    return drv_io_do_name((drv_io_do_t)pin);
}

static int board_count(void)
{
    return drv_io_board_count();
}

static sw_err_t get_stats(int board_id, hal_io_stats_t *out)
{
    drv_io_stats_t raw;

    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    if (drv_io_get_stats(board_id, &raw) != SW_OK)
    {
        return SW_ERR_PARAM;
    }

    out->online                = raw.online;
    out->dirty_pending         = raw.dirty_pending;
    out->offline_count         = raw.offline_count;
    out->online_recover_count  = raw.online_recover_count;
    out->input_refresh_count   = raw.input_refresh_count;
    out->output_request_count  = raw.output_request_count;
    out->output_flush_count    = raw.output_flush_count;
    out->output_resend_count   = raw.output_resend_count;
    out->last_online_ms        = raw.last_online_ms;
    out->last_offline_ms       = raw.last_offline_ms;
    out->last_input_refresh_ms = raw.last_input_refresh_ms;
    out->last_output_req_ms    = raw.last_output_req_ms;
    out->last_output_flush_ms  = raw.last_output_flush_ms;
    out->last_input_snapshot   = raw.last_input_snapshot;
    out->last_output_snapshot  = raw.last_output_snapshot;
    return SW_OK;
}

static const hal_io_ops_t s_ops = {
    .init                     = io_init,
    .start                    = io_start,
    .register_panic_cb        = register_panic_cb,
    .flush_outputs_now        = flush_outputs_now,
    .board_is_online          = board_is_online,
    .wait_boards_online       = wait_boards_online,
    .do_set                   = do_set,
    .di_read                  = di_read,
    .register_debug_input_cb  = register_debug_input_cb,
    .register_board_status_cb = register_board_status_cb,
    .try_parse_di             = try_parse_di,
    .try_parse_do             = try_parse_do,
    .di_name                  = di_name,
    .do_name                  = do_name,
    .board_count              = board_count,
    .get_stats                = get_stats,
};

void hal_io_linux_register(void)
{
    hal_io_register(&s_ops);
}
