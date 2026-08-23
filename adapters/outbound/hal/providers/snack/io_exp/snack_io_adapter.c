/**
 * @file    snack_io_adapter.c
 * @brief   数字 IO HAL 端口 Linux 真机实现（io_exp_driver 转发）
 * @author  HUWANGWEI
 * @date    2026-07-07
 */

#include "adapters/outbound/hal/providers/snack/io_exp/snack_io_adapter.h"

#include "common/log.h"
#include "domain/ports/outbound/hal/hal_io_port.h"

#ifdef SNACK_IO_ADAPTER_UNIT_TEST
#include <string.h>
#endif

static drv_io_cfg_t s_cfg;
static bool         s_configured = false;
static bool         s_inited     = false;

static sw_err_t io_init(void)
{
    sw_err_t ret;

    if (!s_configured) {
        return SW_ERR_NOT_INIT;
    }

    ret = drv_io_cfg_validate(&s_cfg);
    if (ret != SW_OK) {
        LOG_ERROR("snack_io_adapter: invalid cfg ret=%d", (int)ret);
        return ret;
    }

    ret = io_exp_driver_sdk_init(s_cfg.can_bus, s_cfg.can_baud, s_cfg.self_node, s_cfg.board_count);
    if (ret != SW_OK) {
        LOG_ERROR("snack_io_adapter: io_exp sdk init failed ret=%d", (int)ret);
        return ret;
    }

    ret = drv_io_init(&s_cfg);
    if (ret == SW_OK) {
        s_inited = true;
    }
    return ret;
}

static sw_err_t io_start(void)
{
    if (!s_configured) {
        return SW_ERR_NOT_INIT;
    }
    if (!s_inited) {
        return SW_ERR_NOT_INIT;
    }
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

static sw_err_t di_read(io_di_t pin, io_di_sample_t *sample)
{
    return drv_io_di_read(pin, sample);
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
    return drv_io_get_stats(board_id, out);
}

static int pulse_read(io_di_t pin)
{
    return drv_io_pulse_read(pin);
}

static sw_err_t pulse_clear(io_di_t pin)
{
    return drv_io_pulse_clear(pin);
}

static int adc_read(int board_id, int port)
{
    return drv_io_adc_read(board_id, port);
}

static int adc_mv(int board_id, int port)
{
    return drv_io_adc_mv(board_id, port);
}

static int adc_ma(int board_id, int port)
{
    return drv_io_adc_ma(board_id, port);
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
    .adc_read                 = adc_read,
    .adc_mv                   = adc_mv,
    .adc_ma                   = adc_ma,
};

void snack_io_adapter_register(void)
{
    hal_io_register(&s_ops);
}

sw_err_t snack_io_adapter_configure(const drv_io_cfg_t *cfg)
{
    sw_err_t ret;

    if (cfg == NULL) {
        return SW_ERR_PARAM;
    }

    ret = drv_io_cfg_validate(cfg);
    if (ret != SW_OK) {
        return ret;
    }
    if (s_configured) {
        return SW_ERR_BUSY;
    }
    s_cfg        = *cfg;
    s_configured = true;
    s_inited     = false;
    return SW_OK;
}

#ifdef SNACK_IO_ADAPTER_UNIT_TEST
void snack_io_adapter_test_reset(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_configured = false;
    s_inited     = false;
}
#endif
