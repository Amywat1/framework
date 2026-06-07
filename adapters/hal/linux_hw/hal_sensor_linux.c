/**
 * @file    hal_sensor_linux.c
 * @brief   传感器 HAL 端口的 Linux 真机实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_sensor_port.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "ports/hal/hal_io_port.h"
#include "common/log.h"

static void m8_poll_input_events(void)
{
}

static void io_board_status_cb(int board_id, bool offline)
{
    if (offline)
    {
        LOG_WARN("hal_sensor: IO board %d offline", board_id);
    }
    else
    {
        LOG_INFO("hal_sensor: IO board %d online", board_id);
    }
}

static bool m8_gantry_at_fwd_limit(void)  { return m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM); }
static bool m8_gantry_at_rev_limit(void)  { return m8_signal_is_active(M8_SIG_GANTRY_REV_LIM); }
static bool m8_lift_at_top(void)          { return m8_signal_is_active(M8_SIG_LIFT_UP_LIM); }
static bool m8_lift_at_bottom(void)       { return m8_signal_is_active(M8_SIG_LIFT_DOWN_LIM); }

static bool m8_is_estop_active(void)
{
    return m8_signal_is_active(M8_SIG_ESTOP);
}

static const hal_sensor_ops_t s_ops = {
    .gantry_at_fwd_limit  = m8_gantry_at_fwd_limit,
    .gantry_at_rev_limit  = m8_gantry_at_rev_limit,
    .lift_at_top          = m8_lift_at_top,
    .lift_at_bottom       = m8_lift_at_bottom,
    .is_estop_active      = m8_is_estop_active,
    .poll_input_events    = m8_poll_input_events,
};

void hal_sensor_linux_register(void)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops != NULL) && (ops->register_board_status_cb != NULL))
    {
        ops->register_board_status_cb(io_board_status_cb);
    }
    hal_sensor_register(&s_ops);
}
