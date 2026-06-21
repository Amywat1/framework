/**
 * @file    interlock.c
 * @brief   运动互锁条件检查实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "domain/safety/interlock.h"
#include "domain/safety/alarm_core.h"
#include "common/log.h"

sw_err_t interlock_check_motion(motion_type_t type)
{
    /* 最高优先级：ERROR 报警激活时禁止所有运动 */
    if (alarm_core_has_error())
    {
        LOG_WARN("interlock: motion blocked by active ERROR alarm (type=%d)", (int)type);
        return SW_ERR_STATE;
    }

    /*
     * 刷子切换额外检查：
     * 硬件层（HAL）已检查 VFD 是否停止；
     * 此处软件层额外防御：有 WARNING 级别报警时也禁止刷子切换，
     * 避免故障降级运行期间对接触器施加额外冲击。
     */
    if ((type == MOTION_TYPE_BRUSH_SWITCH) && alarm_core_has_warning())
    {
        LOG_WARN("interlock: brush_switch blocked by active WARNING alarm");
        return SW_ERR_STATE;
    }

    return SW_OK;
}
