/**
 * @file    m8_hal_ctx.c
 * @brief   M8 linux_hw 适配器内部共享状态实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "adapters/machine/m8/m8_alarm_adapt.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "config/machine/m8_vfd_table.h"
#include "adapters/hal/linux_hw/drv/drv_vfd.h"
#include "ports/hal/hal_io_port.h"
#include "common/log.h"

#include <string.h>

static sw_err_t io_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, val);
}

/* -------------------------------------------------------------------------
 * VFD 实例（静态分配，由 m8_linux_hw_init 初始化）
 * ------------------------------------------------------------------------- */
static drv_vfd_t s_vfd_brush;
static drv_vfd_t s_vfd_gantry;

/* -------------------------------------------------------------------------
 * VFD 事件路由（驱动层事件 → alarm_core）
 * ------------------------------------------------------------------------- */
static void brush_vfd_event_cb(int event_code)
{
    if (event_code == DRV_VFD_EVT_COMM_LOST)
    {
        m8_alarm_on_vfd_comm_lost(true);
    }
    else if (event_code == DRV_VFD_EVT_COMM_RESTORED)
    {
        m8_alarm_on_vfd_comm_restored(true);
    }
}

static void gantry_vfd_event_cb(int event_code)
{
    if (event_code == DRV_VFD_EVT_COMM_LOST)
    {
        m8_alarm_on_vfd_comm_lost(false);
    }
    else if (event_code == DRV_VFD_EVT_COMM_RESTORED)
    {
        m8_alarm_on_vfd_comm_restored(false);
    }
}

/* -------------------------------------------------------------------------
 * m8_linux_hw_init
 * ------------------------------------------------------------------------- */
sw_err_t m8_linux_hw_init(void)
{
    sw_err_t ret;

    /* 刷子 VFD：仅正转（无反转引脚），接触器上电时全部断开 */
    memset(&s_vfd_brush, 0, sizeof(s_vfd_brush));
    ret = drv_vfd_init(&s_vfd_brush,
                       M8_VFD_BRUSH_SERIAL_PORT, M8_VFD_BRUSH_BAUD, M8_VFD_BRUSH_ADDR,
                       M8_VFD_BRUSH_PIN_FWD,
                       M8_VFD_BRUSH_HAS_REV, M8_VFD_BRUSH_PIN_REV,
                       M8_VFD_BRUSH_PIN_RST);
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_linux_hw_init: vfd_brush init failed");
        return ret;
    }
    drv_vfd_register_event_cb(&s_vfd_brush, brush_vfd_event_cb);
    (void)io_do_set(M8_DO_TOP_BRUSH_ACT,  false);
    (void)io_do_set(M8_DO_SIDE_BRUSH_ACT, false);

    /* 龙门 VFD：支持正反转 */
    memset(&s_vfd_gantry, 0, sizeof(s_vfd_gantry));
    ret = drv_vfd_init(&s_vfd_gantry,
                       M8_VFD_GANTRY_SERIAL_PORT, M8_VFD_GANTRY_BAUD, M8_VFD_GANTRY_ADDR,
                       M8_VFD_GANTRY_PIN_FWD,
                       M8_VFD_GANTRY_HAS_REV, M8_VFD_GANTRY_PIN_REV,
                       M8_VFD_GANTRY_PIN_RST);
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_linux_hw_init: vfd_gantry init failed");
        return ret;
    }
    drv_vfd_register_event_cb(&s_vfd_gantry, gantry_vfd_event_cb);

    LOG_INFO("m8_linux_hw_init ok");
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * VFD 实例访问器
 * ------------------------------------------------------------------------- */
drv_vfd_t *m8_ctx_vfd_brush(void)   { return &s_vfd_brush; }
drv_vfd_t *m8_ctx_vfd_gantry(void)  { return &s_vfd_gantry; }
