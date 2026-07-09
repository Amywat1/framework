/**
 * @file    self_check_service.c
 * @brief   完整自检流程协调实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    阶段一最小桩：仅根据急停/联锁态决定落点并立即发布完成，
 *          不含设计 §6.4 描述的自检项汇总与机构驱动；后续阶段再补。
 */

#include "framework/application/self_check_service.h"
#include "framework/domain/safety/alarm/alarm_core.h"
#include "framework/domain/safety/model/safety_types.h"
#include "framework/domain/command_gateway/operational_mode.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"

static void publish_completed(bool land_exception)
{
    (void)event_publish(EVT_OP_MODE_SELF_CHECK_COMPLETED,
                        land_exception ? 1U : 0U);
}

sw_err_t self_check_service_init(void)
{
    LOG_INFO("self_check_service: init ok");
    return SW_OK;
}

sw_err_t self_check_service_start(void)
{
    bool land_exception = false;

    if (op_mode_is_estop_active())
    {
        land_exception = true;
    }
    else if (alarm_core_safety_state() == SAFETY_STATE_LOCKOUT)
    {
        land_exception = true;
    }

    publish_completed(land_exception);
    LOG_INFO("self_check_service: completed land_exception=%d", (int)land_exception);
    return SW_OK;
}
