/**
 * @file    snack_io_adapter.c
 * @brief   数字 IO HAL 端口 Linux 真机实现（io_exp_driver 转发）
 * @author  HUWANGWEI
 * @date    2026-07-07
 */

#include "framework/adapters/outbound/hal/providers/snack/io_exp/snack_io_adapter.h"

#include "framework/common/log.h"
#include "framework/ports/outbound/hal/hal_io_port.h"

#include <string.h>

/* hal_io_stats_t 与 drv_io_stats_t 字段完全镜像，get_stats 用 memcpy 复制。
 * 若两者大小不同，说明其中一方新增了字段但另一方未同步，编译时报错提醒维护。*/
_Static_assert(sizeof(hal_io_stats_t) == sizeof(drv_io_stats_t),
               "hal_io_stats_t and drv_io_stats_t must remain identical");

static drv_io_cfg_t s_cfg;

static sw_err_t io_init(void)
{
    return drv_io_init(&s_cfg);
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
    return drv_io_do_set(pin, val);
}

static bool di_read(io_di_t pin)
{
    return drv_io_di_read(pin);
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
    return drv_io_try_parse_di(name, out);
}

static bool try_parse_do(const char *name, io_do_t *out)
{
    return drv_io_try_parse_do(name, out);
}

static const char *di_name(io_di_t pin)
{
    return drv_io_di_name(pin);
}

static const char *do_name(io_do_t pin)
{
    return drv_io_do_name(pin);
}

static int board_count(void)
{
    return drv_io_board_count();
}

static sw_err_t get_stats(int board_id, hal_io_stats_t *out)
{
    drv_io_stats_t raw;

    if (out == NULL) {
        return SW_ERR_PARAM;
    }

    if (drv_io_get_stats(board_id, &raw) != SW_OK) {
        return SW_ERR_PARAM;
    }

    memcpy(out, &raw, sizeof(*out));
    return SW_OK;
}

static int pulse_read(io_di_t pin)
{
    return drv_io_pulse_read(pin);
}

static sw_err_t pulse_clear(io_di_t pin)
{
    return drv_io_pulse_clear(pin);
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
    .pulse_read               = pulse_read,
    .pulse_clear              = pulse_clear,
};

void snack_io_adapter_register(const drv_io_cfg_t *cfg)
{
    if (cfg == NULL) {
        LOG_ERROR("snack_io_adapter: register cfg is NULL");
        return;
    }

    s_cfg = *cfg;
    hal_io_register(&s_ops);
}
