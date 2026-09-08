/**
 * @file    estop_alarm_bridge.c
 * @brief   硬件急停确认边沿投影为报警
 * @author  HUWANGWEI
 * @date    2026-09-08
 */

#include "application/bridges/estop_alarm_bridge.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"

static uint32_t s_alarm_code;

static void on_hw_estop_on(const event_t *evt)
{
    (void)evt;
    (void)alarm_registry_trigger(s_alarm_code);
}

static void on_hw_estop_off(const event_t *evt)
{
    (void)evt;
    (void)alarm_registry_clear(s_alarm_code);
}

sw_err_t estop_alarm_bridge_init(uint32_t alarm_code)
{
    static const event_subscription_t s_subs[] = {
        {EVT_HW_ESTOP_ON,  on_hw_estop_on },
        {EVT_HW_ESTOP_OFF, on_hw_estop_off},
    };
    sw_err_t ret;

    if (!alarm_code_is_valid(alarm_code)) {
        return SW_ERR_PARAM;
    }

    s_alarm_code = alarm_code;
    ret          = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        s_alarm_code = 0U;
        return ret;
    }

    LOG_INFO("estop_alarm_bridge: init ok code=%06u", (unsigned)alarm_code);
    return SW_OK;
}
