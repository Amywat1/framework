/**
 * @file    brush.c
 * @brief   刷子设备实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/device/brush.h"
#include "ports/hal/hal_motion_port.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"

static brush_id_t s_active_brush = BRUSH_ID_NONE;
static bool       s_is_running   = false;

sw_err_t brush_init(void)
{
    s_active_brush = BRUSH_ID_NONE;
    s_is_running   = false;
    LOG_INFO("brush: init ok");
    return SW_OK;
}

sw_err_t brush_start(brush_id_t id, uint16_t freq_hz)
{
    const hal_motion_ops_t *ops = hal_motion_get_ops();
    sw_err_t                ret;

    if (id == BRUSH_ID_NONE)
    {
        return SW_ERR_PARAM;
    }

    /* 若当前运行的不是目标刷子，先停 VFD 再切换接触器 */
    if (s_is_running && (s_active_brush != id))
    {
        LOG_INFO("brush: switching %d → %d", (int)s_active_brush, (int)id);
        ret = ops->brush_stop();
        if (ret != SW_OK)
        {
            LOG_ERROR("brush_start: stop failed ret=%d", (int)ret);
            return ret;
        }
        s_is_running = false;
    }

    /* 切换接触器（HAL 内部保证：先断全部 → 等 200ms → 合目标）*/
    if (s_active_brush != id)
    {
        hal_brush_sel_t sel = (id == BRUSH_ID_TOP) ? HAL_BRUSH_TOP : HAL_BRUSH_SIDE;
        ret = ops->brush_select(sel);
        if (ret != SW_OK)
        {
            LOG_ERROR("brush_start: brush_select failed ret=%d", (int)ret);
            return ret;
        }
        s_active_brush = id;
    }

    /* 启动 VFD */
    ret = ops->brush_run(freq_hz);
    if (ret != SW_OK)
    {
        LOG_ERROR("brush_start: brush_run failed ret=%d", (int)ret);
        return ret;
    }

    s_is_running = true;

    (void)event_publish(EVT_COMP_BRUSH_STARTED, (uint32_t)id);
    LOG_INFO("brush: started id=%d freq=%u", (int)id, (unsigned)freq_hz);
    return SW_OK;
}

sw_err_t brush_stop(void)
{
    const hal_motion_ops_t *ops = hal_motion_get_ops();
    sw_err_t                ret;

    if (!s_is_running)
    {
        return SW_OK;
    }

    ret = ops->brush_stop();
    if (ret == SW_OK)
    {
        s_is_running = false;
        LOG_INFO("brush: stopped");
    }
    return ret;
}

sw_err_t brush_off(void)
{
    (void)brush_stop();
    /* 重置内部状态（HAL 侧接触器在下次 brush_select 前保持原状，属于 HAL 职责）*/
    s_active_brush = BRUSH_ID_NONE;
    LOG_INFO("brush: off (state reset)");
    return SW_OK;
}

bool brush_is_running(brush_id_t id)
{
    return s_is_running && (s_active_brush == id);
}

brush_id_t brush_get_active(void)
{
    return s_active_brush;
}
