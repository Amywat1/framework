/**
 * @file    hal_sensor_linux.c
 * @brief   传感器 HAL 端口的 Linux 真机实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_sensor_port.h"
#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "domain/model/alarm_code.h"
#include "driver/drv_io.h"
#include "driver/drv_vfd.h"
#include "core/event_bus/event_bus.h"
#include "common/log.h"

/* -------------------------------------------------------------------------
 * IO 输入事件轮询（正式运行链路）
 * 不依赖 drv_io 的调试输入回调；统一从输入缓存读值后做边沿提取。
 * 急停、限位等离散量已由 m8_signal_filter_tick() 统一做防抖与事件发布，
 * 此处只保留编码器脉冲的边沿提取。
 * ------------------------------------------------------------------------- */
static void m8_poll_input_events(void)
{
    static bool s_init             = false;
    static bool s_prev_encoder     = false;

    bool encoder      = drv_io_di_read(M8_DI_ENCODER);

    if (!s_init)
    {
        s_prev_encoder    = encoder;
        s_init            = true;
        return;
    }

    if (encoder && !s_prev_encoder)
    {
        m8_ctx_encoder_tick();
        (void)event_publish(EVT_HW_ENCODER_TICK, m8_ctx_gantry_is_fwd() ? 1U : 0U);
    }

    s_prev_encoder    = encoder;
}

/* IO 子板在线状态变化回调 */
static void io_board_status_cb(int board_id, bool offline)
{
    (void)event_publish(offline ? EVT_HW_IO_OFFLINE : EVT_HW_IO_ONLINE,
                        (uint32_t)board_id);
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

static int32_t m8_get_gantry_pos(void)   { return m8_ctx_get_gantry_pos(); }
static void    m8_reset_gantry_pos(void) { m8_ctx_reset_gantry_pos(); }

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
    .get_gantry_pos       = m8_get_gantry_pos,
    .reset_gantry_pos     = m8_reset_gantry_pos,
    .poll_input_events    = m8_poll_input_events,
    .get_vfd_fault_code   = m8_get_vfd_fault_code,
};

void hal_sensor_linux_register(void)
{
    drv_io_register_board_error_cb(io_board_status_cb);
    hal_sensor_register(&s_ops);
}
