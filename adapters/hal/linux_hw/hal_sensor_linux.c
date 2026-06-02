/**
 * @file    hal_sensor_linux.c
 * @brief   传感器 HAL 端口的 Linux 真机实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_sensor_port.h"
#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "adapters/hal/linux_hw/drv/drv_io.h"
#include "adapters/hal/linux_hw/drv/drv_vfd.h"
#include "common/log.h"

static void m8_poll_input_events(void)
{
    /* 真机路径下编码器由硬件脉冲计数器完成，接口保留供报警轮询调用。 */
}

/* IO 子板在线状态变化回调（离线检测由 drv_io / alarm 链路负责） */
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

/* -------------------------------------------------------------------------
 * sensor_ops 实现
 * ------------------------------------------------------------------------- */
static bool m8_gantry_at_fwd_limit(void)  { return m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM); }
static bool m8_gantry_at_rev_limit(void)  { return m8_signal_is_active(M8_SIG_GANTRY_REV_LIM); }
static bool m8_lift_at_top(void)          { return m8_signal_is_active(M8_SIG_LIFT_UP_LIM); }
static bool m8_lift_at_bottom(void)       { return m8_signal_is_active(M8_SIG_LIFT_DOWN_LIM); }

static bool m8_is_estop_active(void)
{
    /* 极性已在 m8_signal_table 中统一配置 */
    return m8_signal_is_active(M8_SIG_ESTOP);
}

static sw_err_t m8_get_vfd_fault_code(hal_vfd_id_t vfd_id, uint16_t *p_code)
{
    if (p_code == NULL)
    {
        return SW_ERR_PARAM;
    }
    drv_vfd_t *vfd = (vfd_id == HAL_VFD_BRUSH) ? m8_ctx_vfd_brush()
                                                : m8_ctx_vfd_gantry();
    return drv_vfd_get_fault_code(vfd, p_code);
}

/* -------------------------------------------------------------------------
 * 操作表 + 注册
 * ------------------------------------------------------------------------- */
static const hal_sensor_ops_t s_ops = {
    .gantry_at_fwd_limit  = m8_gantry_at_fwd_limit,
    .gantry_at_rev_limit  = m8_gantry_at_rev_limit,
    .lift_at_top          = m8_lift_at_top,
    .lift_at_bottom       = m8_lift_at_bottom,
    .is_estop_active      = m8_is_estop_active,
    .poll_input_events    = m8_poll_input_events,
    .get_vfd_fault_code   = m8_get_vfd_fault_code,
};

void hal_sensor_linux_register(void)
{
    drv_io_register_board_error_cb(io_board_status_cb);
    hal_sensor_register(&s_ops);
}
