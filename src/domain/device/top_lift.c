/**
 * @file    top_lift.c
 * @brief   顶刷升降设备实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/device/top_lift.h"
#include "domain/safety/interlock.h"
#include "ports/hal/hal_motion_port.h"
#include "ports/hal/hal_sensor_port.h"
#include "common/log.h"

sw_err_t top_lift_init(void)
{
    LOG_INFO("top_lift: init ok");
    return SW_OK;
}

sw_err_t top_lift_up_start(uint32_t pulses)
{
    const hal_motion_ops_t *ops = hal_motion_get_ops();
    sw_err_t                ret = interlock_check_motion(MOTION_TYPE_LIFT_UP);
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("top_lift: up_start pulses=%u", (unsigned)pulses);

    /*
     * HAL Phase 3 实现：同步执行脉冲序列，完成后发 EVT_COMP_LIFT_DONE 再返回。
     * HAL Phase 5 实现：投递命令到 stepper_thread，立即返回。
     * 上层通过 EVT_COMP_LIFT_DONE 感知完成，此接口保持一致。
     */
    return ops->lift_up_start(pulses);
}

sw_err_t top_lift_down_start(uint32_t pulses)
{
    const hal_motion_ops_t *ops = hal_motion_get_ops();
    sw_err_t                ret = interlock_check_motion(MOTION_TYPE_LIFT_DOWN);
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("top_lift: down_start pulses=%u", (unsigned)pulses);
    return ops->lift_down_start(pulses);
}

bool top_lift_at_top(void)
{
    return hal_sensor_get_ops()->lift_at_top();
}

bool top_lift_at_bottom(void)
{
    return hal_sensor_get_ops()->lift_at_bottom();
}
