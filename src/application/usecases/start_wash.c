/**
 * @file    start_wash.c
 * @brief   启动洗车用例实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "application/usecases/start_wash.h"
#include "service/dev_ctx/dev_ctx.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"

sw_err_t start_wash(wash_mode_t mode)
{
    device_context_t ctx = dev_ctx_snapshot();

    if (ctx.safety_state != SAFETY_STATE_OK)
    {
        LOG_WARN("start_wash: rejected (safety_state=%d)", (int)ctx.safety_state);
        return SW_ERR_STATE;
    }
    if (ctx.device_state != DEV_STATE_IDLE)
    {
        LOG_WARN("start_wash: rejected (device_state=%d)", (int)ctx.device_state);
        return SW_ERR_STATE;
    }

    return event_publish(EVT_CMD_ORDER, (uint32_t)mode);
}
