/**
 * @file    comp_brush.c
 * @brief   刷子组件实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "comp_brush.h"
#include "bsp/bsp_hal.h"
#include "common/log.h"
#include "service/svc_param.h"

static BrushId_t s_active_brush = BRUSH_ID_MAX;   /* BRUSH_ID_MAX = 无接入 */
static bool      s_is_running   = false;

sw_err_t comp_brush_init(void)
{
    s_active_brush = BRUSH_ID_MAX;
    s_is_running   = false;
    LOG_INFO("comp_brush init ok");
    return SW_OK;
}

sw_err_t comp_brush_start(BrushId_t id, uint16_t freq_hz)
{
    sw_err_t ret;

    if (id >= BRUSH_ID_MAX) {
        return SW_ERR_PARAM;
    }

    /* 若当前运行的不是目标刷子，需先停转再切换接触器 */
    if (s_is_running && (s_active_brush != id)) {
        LOG_INFO("comp_brush: switching from %d to %d", (int)s_active_brush, (int)id);
        ret = hal_brush_stop();
        if (ret != SW_OK) {
            return ret;
        }
        s_is_running = false;
    }

    /* 切换接触器（选择目标刷子接入 VFD）*/
    if (s_active_brush != id) {
        hal_brush_sel_t sel = (id == BRUSH_ID_TOP) ? HAL_BRUSH_SEL_TOP : HAL_BRUSH_SEL_SIDE;
        ret = hal_brush_select(sel);
        if (ret != SW_OK) {
            LOG_ERROR("comp_brush_start: select failed ret=%d", (int)ret);
            return ret;
        }
        s_active_brush = id;
    }

    /* 启动 VFD */
    ret = hal_brush_run(freq_hz);
    if (ret != SW_OK) {
        return ret;
    }

    s_is_running = true;
    LOG_INFO("comp_brush_start: brush=%d freq=%u ok", (int)id, (unsigned)freq_hz);
    return SW_OK;
}

sw_err_t comp_brush_stop(void)
{
    if (!s_is_running) {
        return SW_OK;
    }
    sw_err_t ret = hal_brush_stop();
    if (ret == SW_OK) {
        s_is_running = false;
    }
    return ret;
}

sw_err_t comp_brush_off(void)
{
    (void)comp_brush_stop();
    /* 断开所有接触器 */
    (void)hal_do_set(DO_TOP_BRUSH_ACT,  false);
    (void)hal_do_set(DO_SIDE_BRUSH_ACT, false);
    s_active_brush = BRUSH_ID_MAX;
    return SW_OK;
}

bool comp_brush_is_running(BrushId_t id)
{
    return s_is_running && (s_active_brush == id);
}

BrushId_t comp_brush_get_active(void)
{
    return s_active_brush;
}
