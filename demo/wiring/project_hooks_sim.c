/**
 * @file    project_hooks_sim.c
 * @brief   Demo 仿真项目生命周期钩子
 */

#include "adapters/inbound/safety/estop_poll_thread.h"
#include "adapters/outbound/storage/json/json_deploy_store.h"
#include "adapters/outbound/storage/json/json_param_store.h"
#include "runtime/ports/port_contract.h"
#include "runtime/bootstrap/project_hooks.h"

extern sw_err_t demo_device_ops_register(void);
extern sw_err_t demo_alarm_catalog_load(void);

static sw_err_t configure_storage(void)
{
    sw_err_t r;

    r = json_param_store_configure(PARAM_STORE_JSON_FILE_PATH);
    if (r != SW_OK) {
        return r;
    }
    return json_deploy_store_configure(DEPLOY_STORE_JSON_FILE_PATH);
}

static sw_err_t bind_device(void)
{
    return demo_device_ops_register();
}

static sw_err_t bind_alarm_catalog(void)
{
    return demo_alarm_catalog_load();
}

static sw_err_t validate(void)
{
    return port_contract_validate(PORT_REQ_HAL_IO | PORT_REQ_HAL_VOICE | PORT_REQ_PARAM_STORE | PORT_REQ_DEPLOY_STORE
                                  | PORT_REQ_DEVICE_OPS | PORT_REQ_ALARM_BINDING | PORT_REQ_SAFETY);
}

/*
 * 接入框架自带的急停轮询适配器。
 * 放在 init_adapters：thread_register 只登记不创建，真正起线程由 start 阶段负责。
 */
static sw_err_t init_adapters(void)
{
    return estop_poll_thread_init(NULL);
}

sw_err_t project_hooks_register(void)
{
    static const project_hooks_t s_hooks = {
        .configure_storage      = configure_storage,
        .configure_hal          = project_hook_noop,
        .bind_hal               = project_hook_noop,
        .init_hal               = project_hook_noop,
        .configure_safety       = project_hook_noop,
        .init_safety            = project_hook_noop,
        .configure_adapters     = project_hook_noop,
        .bind_device           = bind_device,
        .init_device           = project_hook_noop,
        .bind_alarm_catalog     = bind_alarm_catalog,
        .validate               = validate,
        .init_adapters          = init_adapters,
        .register_runtime_tasks = project_hook_noop,
        .start_runtime          = project_hook_noop,
        .assert_safe_outputs    = project_hook_noop_void,
    };
    return bootstrap_register_hooks(&s_hooks);
}
