/**
 * @file    hal_motion_linux.c
 * @brief   运动控制 HAL 端口 — Linux 真机实现（龙门/刷子/顶刷升降）
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_motion_port.h"
#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "driver/drv_vfd.h"
#include "driver/drv_stepper.h"
#include "driver/drv_io.h"
#include "core/event_bus/event_bus.h"
#include "common/log.h"
#include "common/sw_error.h"

#include <unistd.h>  /* usleep（接触器等待，非精度关键路径）*/

/* -------------------------------------------------------------------------
 * 龙门 VFD
 * ------------------------------------------------------------------------- */
static sw_err_t m8_gantry_fwd(uint16_t freq_hz)
{
    m8_ctx_set_gantry_fwd(true);
    return drv_vfd_run_fwd(m8_ctx_vfd_gantry(), freq_hz);
}

static sw_err_t m8_gantry_rev(uint16_t freq_hz)
{
    m8_ctx_set_gantry_fwd(false);
    return drv_vfd_run_rev(m8_ctx_vfd_gantry(), freq_hz);
}

static sw_err_t m8_gantry_stop(void)
{
    return drv_vfd_stop(m8_ctx_vfd_gantry());
}

static sw_err_t m8_gantry_fault_reset(void)
{
    return drv_vfd_fault_reset(m8_ctx_vfd_gantry());
}

/* -------------------------------------------------------------------------
 * 刷子 VFD + 接触器
 * ------------------------------------------------------------------------- */
static sw_err_t m8_brush_select(hal_brush_sel_t sel)
{
    if (drv_vfd_get_state(m8_ctx_vfd_brush()) == DRV_VFD_STATE_FWD)
    {
        LOG_ERROR("hal_motion: brush_select called while VFD running");
        return SW_ERR_STATE;
    }

    /* 先断开全部接触器，等 200ms 防止同时吸合 */
    (void)drv_io_do_set(M8_DO_TOP_BRUSH_ACT,  false);
    (void)drv_io_do_set(M8_DO_SIDE_BRUSH_ACT, false);
    usleep((unsigned long)M8_BRUSH_CONTACTOR_WAIT_MS * 1000UL);

    if (sel == HAL_BRUSH_TOP)
    {
        (void)drv_io_do_set(M8_DO_TOP_BRUSH_ACT, true);
    }
    else
    {
        (void)drv_io_do_set(M8_DO_SIDE_BRUSH_ACT, true);
    }

    LOG_INFO("hal_motion: brush_select sel=%d", (int)sel);
    return SW_OK;
}

static sw_err_t m8_brush_run(uint16_t freq_hz)
{
    return drv_vfd_run_fwd(m8_ctx_vfd_brush(), freq_hz);
}

static sw_err_t m8_brush_stop(void)
{
    return drv_vfd_stop(m8_ctx_vfd_brush());
}

static sw_err_t m8_brush_fault_reset(void)
{
    return drv_vfd_fault_reset(m8_ctx_vfd_brush());
}

/* -------------------------------------------------------------------------
 * 顶刷升降当前在调用线程中同步执行。
 *
 * 动作完成后发布 EVT_COMP_LIFT_DONE 事件。
 * ------------------------------------------------------------------------- */
static sw_err_t m8_lift_up_start(uint32_t pulses)
{
    uint32_t remaining = (pulses > 0U) ? pulses : M8_LIFT_UP_MAX_PULSES;
    sw_err_t ret       = SW_OK;

    (void)drv_stepper_enable();

    while (remaining > 0U)
    {
        uint32_t batch = (remaining > M8_STEPPER_PULSE_BATCH)
                         ? M8_STEPPER_PULSE_BATCH : remaining;

        ret = drv_stepper_move(batch, STEPPER_DIR_UP, M8_STEPPER_PULSE_US);
        if (ret != SW_OK) { break; }
        remaining -= batch;

        /* 每批后检查上限位 */
        if (drv_io_di_read(M8_DI_LIFT_UP_LIM))
        {
            LOG_INFO("hal_motion: lift_up reached top limit");
            break;
        }
    }

    (void)drv_stepper_disable();
    (void)event_publish(EVT_COMP_LIFT_DONE, (uint32_t)ret);
    return SW_OK;  /* 当前行为：先发布 EVT_COMP_LIFT_DONE，再返回 */
}

static sw_err_t m8_lift_down_start(uint32_t pulses)
{
    uint32_t remaining = (pulses > 0U) ? pulses : M8_LIFT_DOWN_DEF_PULSES;
    sw_err_t ret       = SW_OK;

    (void)drv_stepper_enable();

    while (remaining > 0U)
    {
        uint32_t batch = (remaining > M8_STEPPER_PULSE_BATCH)
                         ? M8_STEPPER_PULSE_BATCH : remaining;

        ret = drv_stepper_move(batch, STEPPER_DIR_DOWN, M8_STEPPER_PULSE_US);
        if (ret != SW_OK) { break; }
        remaining -= batch;

        /* 每批后检查下限位（防止过冲）*/
        if (drv_io_di_read(M8_DI_LIFT_DOWN_LIM))
        {
            LOG_INFO("hal_motion: lift_down reached bottom limit");
            break;
        }
    }

    (void)drv_stepper_disable();
    (void)event_publish(EVT_COMP_LIFT_DONE, (uint32_t)ret);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 操作表 + 注册
 * ------------------------------------------------------------------------- */
static const hal_motion_ops_t s_ops = {
    .gantry_fwd         = m8_gantry_fwd,
    .gantry_rev         = m8_gantry_rev,
    .gantry_stop        = m8_gantry_stop,
    .gantry_fault_reset = m8_gantry_fault_reset,
    .brush_select       = m8_brush_select,
    .brush_run          = m8_brush_run,
    .brush_stop         = m8_brush_stop,
    .brush_fault_reset  = m8_brush_fault_reset,
    .lift_up_start      = m8_lift_up_start,
    .lift_down_start    = m8_lift_down_start,
};

void hal_motion_linux_register(void)
{
    hal_motion_register(&s_ops);
}
