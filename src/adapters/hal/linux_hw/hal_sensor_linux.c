/**
 * @file    hal_sensor_linux.c
 * @brief   传感器 HAL 端口 — Linux 真机实现（限位/急停/编码器/VFD故障）
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_sensor_port.h"
#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "driver/drv_io.h"
#include "driver/drv_vfd.h"
#include "core/event_bus/event_bus.h"

/* -------------------------------------------------------------------------
 * IO 输入变化回调（由 drv_io 后台线程调用）
 *
 * 负责：
 *  1. 发布硬件事件到 event_bus
 *  2. 更新龙门位置计数器（编码器脉冲）
 * ------------------------------------------------------------------------- */
static void io_input_cb(int io_id, bool state)
{
    switch ((drv_io_di_t)io_id)
    {
        case DI_ESTOP:
            /* 常闭接法：DI 变 false = 急停按下，变 true = 释放 */
            (void)event_publish(!state ? EVT_HW_ESTOP_ON : EVT_HW_ESTOP_OFF, 0U);
            break;

        case DI_GANTRY_FWD_LIMIT:
            if (state)
            {
                (void)event_publish(EVT_HW_GANTRY_FWD_LIM, 0U);
            }
            break;

        case DI_GANTRY_REAR_LIMIT:
            if (state)
            {
                (void)event_publish(EVT_HW_GANTRY_REV_LIM, 0U);
            }
            break;

        case DI_TOP_LIFT_UP:
            if (state)
            {
                (void)event_publish(EVT_HW_LIFT_UP_LIM, 0U);
            }
            break;

        case DI_TOP_LIFT_DOWN:
            if (state)
            {
                (void)event_publish(EVT_HW_LIFT_DOWN_LIM, 0U);
            }
            break;

        case DI_ENCODER_PULSE:
            /* 上升沿 = 有效脉冲 */
            if (state)
            {
                m8_ctx_encoder_tick();
                (void)event_publish(EVT_HW_ENCODER_TICK,
                                    m8_ctx_gantry_is_fwd() ? 1U : 0U);
            }
            break;

        default:
            break;
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
static bool m8_lift_at_top(void)           { return drv_io_di_read(M8_DI_LIFT_UP_LIM); }
static bool m8_lift_at_bottom(void)        { return drv_io_di_read(M8_DI_LIFT_DOWN_LIM); }

static bool m8_is_estop_active(void)
{
    /* 常闭接法：DI_ESTOP = false → 急停有效 */
    return !drv_io_di_read(M8_DI_ESTOP);
}

static int32_t m8_get_gantry_pos(void)    { return m8_ctx_get_gantry_pos(); }
static void    m8_reset_gantry_pos(void)  { m8_ctx_reset_gantry_pos(); }

static sw_err_t m8_get_vfd_fault_code(hal_vfd_id_t vfd_id, uint16_t *p_code)
{
    drv_vfd_t *vfd = (vfd_id == HAL_VFD_BRUSH) ? m8_ctx_vfd_brush()
                                                : m8_ctx_vfd_gantry();
    *p_code = drv_vfd_get_fault_code(vfd);
    return SW_OK;
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
};

void hal_sensor_linux_register(void)
{
    /* 注册 IO 输入回调（此处注册，由 drv_io 后台线程触发）*/
    drv_io_register_input_cb(io_input_cb);
    drv_io_register_board_error_cb(io_board_status_cb);
    hal_sensor_register(&s_ops);
}
