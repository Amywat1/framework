/**
 * @file    alarm_binding_bridge.c
 * @brief   报警入站端口 → alarm_registry 的应用层绑定
 * @author  HUWANGWEI
 * @date    2026-08-08
 */

#include "application/bridges/alarm_binding_bridge.h"

#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "common/log.h"
#include "domain/safety/alarm_registry/alarm_registry.h"

static sw_err_t binding_load_catalog(const alarm_def_t *defs, unsigned count)
{
    return alarm_registry_load_catalog(defs, count);
}

sw_err_t alarm_binding_bridge_bind(void)
{
    static const alarm_binding_ops_t s_ops = {
        .trigger      = alarm_registry_trigger,
        .clear        = alarm_registry_clear,
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
