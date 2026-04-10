/**
 * @file    reset_fault.c
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "application/usecases/reset_fault.h"
#include "service/dev_ctx/dev_ctx.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"

sw_err_t reset_fault(void)
{
    device_context_t ctx = dev_ctx_snapshot();

    if (ctx.device_state != DEV_STATE_FAULT)
    {
        LOG_WARN("reset_fault: rejected (device_state=%d)", (int)ctx.device_state);
        return SW_ERR_STATE;
    }

    return event_publish(EVT_CMD_RESET_FAULT, 0U);
}
