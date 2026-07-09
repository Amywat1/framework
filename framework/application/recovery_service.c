/**
 * @file    recovery_service.c
 * @brief   Recover 用例协调实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    阶段一最小桩：仅清除报警并发布完成事件，不含设计 §6.3 描述的
 *          真实机构归位与并行驱动逻辑；后续阶段再补物理动作。
 */

#include "framework/application/recovery_service.h"
#include "framework/domain/safety/alarm/alarm_core.h"
#include "framework/domain/safety/model/safety_types.h"
#include "framework/domain/command_gateway/op_mode_types.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"

static void on_recovery_requested(const event_t *evt)
{
    recovery_result_t result = RECOVERY_RESULT_IDLE;

    (void)evt;

    if (alarm_core_safety_state() == SAFETY_STATE_LOCKOUT)
    {
        result = RECOVERY_RESULT_EXCEPTION;
    }
    else
    {
        (void)alarm_core_reset_alarms();
        if (alarm_core_safety_state() == SAFETY_STATE_LOCKOUT)
        {
            result = RECOVERY_RESULT_EXCEPTION;
        }
    }

    (void)event_publish(EVT_OP_MODE_RECOVERY_COMPLETED, (uint32_t)result);
    LOG_INFO("recovery_service: completed result=%d", (int)result);
}

sw_err_t recovery_service_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_OP_MODE_RECOVERY_REQUESTED, on_recovery_requested },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("recovery_service: init ok");
    return SW_OK;
}
