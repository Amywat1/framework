/**
 * @file    alarm_binding_bridge.c
 * @brief   报警入站端口 → alarm_registry 的应用层绑定
 * @author  HUWANGWEI
 * @date    2026-08-08
 */

#include "application/bridges/alarm_binding_bridge.h"

#include "application/bridges/alarm_bridge.h"
#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "common/log.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"

/**
 * @brief  姿态为 LOCKOUT 时立刻排空 pending，使 EVT_SAFETY_LOCKOUT 在返回前入队
 * @note   须在 registry 调用已经返回（已放锁）之后调用，锁序为 drain → registry。
 */
static void drain_if_lockout(void)
{
    if (alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT) {
        alarm_bridge_drain();
    }
}

static sw_err_t binding_trigger(uint32_t alarm_code)
{
    sw_err_t ret = alarm_registry_trigger(alarm_code);

    if (ret == SW_OK) {
        drain_if_lockout();
    }
    return ret;
}

static sw_err_t binding_clear(uint32_t alarm_code)
{
    sw_err_t ret = alarm_registry_clear(alarm_code);

    if (ret == SW_OK) {
        drain_if_lockout();
    }
    return ret;
}

static sw_err_t binding_load_catalog(const alarm_def_t *defs, unsigned count)
{
    return alarm_registry_load_catalog(defs, count);
}

sw_err_t alarm_binding_bridge_bind(void)
{
    static const alarm_binding_ops_t s_ops = {
        .trigger      = binding_trigger,
        .clear        = binding_clear,
        .load_catalog = binding_load_catalog,
    };

    sw_err_t ret = alarm_binding_register(&s_ops);
    if (ret != SW_OK) {
        LOG_ERROR("alarm_binding_bridge: register ret=%d", (int)ret);
        return ret;
    }
    LOG_INFO("alarm_binding_bridge: bound to alarm_registry");
    return SW_OK;
}
