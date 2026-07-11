/**
 * @file    wash_projection.c
 * @brief   洗车会话 → wash_snapshot 投影实现
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "framework/application/wash_projection.h"
#include "framework/domain/telemetry/snapshot/wash_snapshot_internal.h"
#include "framework/domain/wash/model/wash_types.h"
#include "framework/common/event_types.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/log.h"

static void on_session_started(const event_t *evt)
{
    wash_mode_t mode = (wash_mode_t)(evt->param & 0xFFU);

    wash_snapshot_on_session_started(mode);
}

sw_err_t wash_projection_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_WASH_SESSION_STARTED, on_session_started },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("wash_projection: init ok");
    return SW_OK;
}
