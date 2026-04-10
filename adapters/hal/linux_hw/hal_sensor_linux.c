/**
 * @file    hal_sensor_linux.c
 * @brief   传感器 HAL 端口的 Linux 真机实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_sensor_port.h"
#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "domain/model/alarm_code.h"
#include "driver/drv_io.h"
#include "driver/drv_vfd.h"
#include "core/event_bus/event_bus.h"
#include "common/log.h"

/* -------------------------------------------------------------------------
 * IO 输入变化回调（由 drv_io 后台线程调用）
 * 使用 M8_DI_* 别名，不直接混用原始 DI_* 枚举
 * ------------------------------------------------------------------------- */
static void io_input_cb(int io_id, bool state)
{
    if (io_id == (int)M8_DI_ESTOP)
    {
        /* 常闭接法：DI 变 false = 急停按下，变 true = 释放 */
        (void)event_publish(!state ? EVT_HW_ESTOP_ON : EVT_HW_ESTOP_OFF, 0U);
    }
    else if (io_id == (int)M8_DI_GANTRY_FWD_LIM && state)
    {
        (void)event_publish(EVT_HW_GANTRY_FWD_LIM, 0U);
    }
    else if (io_id == (int)M8_DI_GANTRY_REV_LIM && state)
    {
        (void)event_publish(EVT_HW_GANTRY_REV_LIM, 0U);
    }
    else if (io_id == (int)M8_DI_LIFT_UP_LIM && state)
    {
        (void)event_publish(EVT_HW_LIFT_UP_LIM, 0U);
    }
    else if (io_id == (int)M8_DI_LIFT_DOWN_LIM && state)
    {
        (void)event_publish(EVT_HW_LIFT_DOWN_LIM, 0U);
    }
    else if (io_id == (int)M8_DI_ENCODER && state)
    {
        /* 上升沿 = 有效脉冲 */
        m8_ctx_encoder_tick();
        (void)event_publish(EVT_HW_ENCODER_TICK, m8_ctx_gantry_is_fwd() ? 1U : 0U);
    }
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
static bool m8_gantry_at_fwd_limit(void)  { return drv_io_di_read(M8_DI_GANTRY_FWD_LIM); }
static bool m8_gantry_at_rev_limit(void)  { return drv_io_di_read(M8_DI_GANTRY_REV_LIM); }
static bool m8_lift_at_top(void)          { return drv_io_di_read(M8_DI_LIFT_UP_LIM); }
static bool m8_lift_at_bottom(void)       { return drv_io_di_read(M8_DI_LIFT_DOWN_LIM); }

static bool m8_is_estop_active(void)
{
    /* 常闭接法：DI = false → 急停有效 */
    return !drv_io_di_read(M8_DI_ESTOP);
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
 * VFD 故障轮询钩子，由报警轮询路径定期调用。
 *
 * m8_alarm_adapt 会在 alarm_core 的轮询回调中调用此函数。
 * 读取故障码后通过 event_bus 发布 EVT_HW_VFD_BRUSH_FAULT / GANTRY_FAULT。
 * 通信失败时只记录日志，不误报故障（避免 Modbus 抖动触发报警）。
 * ------------------------------------------------------------------------- */
static void m8_poll_vfd_faults(void)
{
    uint16_t code = 0U;

    if (drv_vfd_get_fault_code(m8_ctx_vfd_brush(), &code) == SW_OK)
    {
        if (code != 0U)
        {
            LOG_WARN("hal_sensor: brush VFD fault code=0x%04X", (unsigned)code);
            (void)event_publish(EVT_HW_VFD_BRUSH_FAULT, ALARM_CODE_VFD_BRUSH);
        }
    }
    /* 通信失败时不发报警事件，避免因总线繁忙产生误报 */

    code = 0U;
    if (drv_vfd_get_fault_code(m8_ctx_vfd_gantry(), &code) == SW_OK)
    {
        if (code != 0U)
        {
            LOG_WARN("hal_sensor: gantry VFD fault code=0x%04X", (unsigned)code);
            (void)event_publish(EVT_HW_VFD_GANTRY_FAULT, ALARM_CODE_VFD_GANTRY);
        }
    }
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
    .get_vfd_fault_code   = m8_get_vfd_fault_code,
    .poll_vfd_faults      = m8_poll_vfd_faults,
};

void hal_sensor_linux_register(void)
{
    drv_io_register_input_cb(io_input_cb);
    drv_io_register_board_error_cb(io_board_status_cb);
    hal_sensor_register(&s_ops);
}
