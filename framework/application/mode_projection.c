/**
 * @file    mode_projection.c
 * @brief   运行模式 → dev_ctx 投影实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/application/mode_projection.h"
#include "framework/domain/command_gateway/operational_mode.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/common/event_types.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/log.h"

static void sync_context_fields(void)
{
    dev_ctx_set_estop_active(op_mode_is_estop_active());
    dev_ctx_set_service_enabled(op_mode_is_service_enabled());
}

static void on_mode_changed(const event_t *evt)
{
    operational_mode_t from = (operational_mode_t)((evt->param >> 8) & 0xFFU);
    operational_mode_t to   = (operational_mode_t)(evt->param & 0xFFU);

    (void)from;
    dev_ctx_set_operational_mode(to);
    sync_context_fields();
}

static void on_context_sync(const event_t *evt)
{
    (void)evt;
    sync_context_fields();
}

sw_err_t mode_projection_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_OP_MODE_CHANGED,     on_mode_changed   },
        { EVT_OP_MODE_CONTEXT_SYNC, on_context_sync  },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    dev_ctx_set_operational_mode(op_mode_get_current());
    sync_context_fields();
    LOG_INFO("mode_projection: init ok");
    return SW_OK;
}
