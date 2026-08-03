/**
 * @file    alarm_lifecycle_bridge.c
 * @brief   报警注册表洗车会话生命周期桥接实现
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#include "application/bridges/alarm_lifecycle_bridge.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "runtime/event_bus/event_bus.h"

static void on_wash_session_started(const event_t *evt)
{
    (void)evt;
    alarm_registry_on_wash_session_started();
}

static void on_wash_session_ended(const event_t *evt)
{
    (void)evt;
    alarm_registry_on_wash_session_ended();
}

sw_err_t alarm_lifecycle_bridge_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_WASH_SESSION_STARTED, on_wash_session_started},
        {EVT_WASH_DONE,            on_wash_session_ended  },
        {EVT_WASH_ABORTED,         on_wash_session_ended  },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("alarm_lifecycle_bridge: init ok");
    return SW_OK;
}
