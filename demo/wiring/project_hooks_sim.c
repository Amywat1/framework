/**
 * @file    project_hooks_sim.c
 * @brief   Demo 仿真项目生命周期钩子
 */

#include "adapters/inbound/safety/estop_poll_thread.h"
#include "adapters/outbound/storage/json/json_deploy_store.h"
#include "adapters/outbound/storage/json/json_param_store.h"
#include "runtime/ports/port_contract.h"
#include "runtime/bootstrap/project_hooks.h"

extern sw_err_t demo_machine_ops_register(void);
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

static sw_err_t configure_hal(void)
{
    return SW_OK;
}

static sw_err_t bind_hal(void)
{
    return SW_OK;
}

static sw_err_t init_hal(void)
{
    return SW_OK;
}

static sw_err_t configure_safety(void)
{
    return SW_OK;
}

static sw_err_t init_safety(void)
{
    return SW_OK;
}

static sw_err_t configure_adapters(void)
{
    return SW_OK;
}

static sw_err_t bind_machine(void)
{
    return demo_machine_ops_register();
}

static sw_err_t init_machine(void)
{
    return SW_OK;
}

static sw_err_t bind_alarm_catalog(void)
{
    return demo_alarm_catalog_load();
}

static sw_err_t validate(void)
{
    return port_contract_validate(PORT_REQ_HAL_IO | PORT_REQ_HAL_VOICE | PORT_REQ_PARAM_STORE | PORT_REQ_DEPLOY_STORE
                                  | PORT_REQ_MACHINE_OPS | PORT_REQ_ALARM_BINDING | PORT_REQ_SAFETY);
}

/*
 * 接入框架自带的急停轮询适配器。
 * 放在 init_adapters：thread_register 只登记不创建，真正起线程由 start 阶段负责。
 */
static sw_err_t init_adapters(void)
{
    return estop_poll_thread_init();
}

static sw_err_t register_runtime_tasks(void)
{
    return SW_OK;
}

static sw_err_t start_runtime(void)
{
    return SW_OK;
}

static void assert_safe_outputs(void)
{
}

sw_err_t project_hooks_register(void)
{
    static const project_hooks_t s_hooks = {
        .configure_storage      = configure_storage,
        .configure_hal          = configure_hal,
        .bind_hal               = bind_hal,
        .init_hal               = init_hal,
        .configure_safety       = configure_safety,
        .init_safety            = init_safety,
        .configure_adapters     = configure_adapters,
        .bind_machine           = bind_machine,
        .init_machine           = init_machine,
        .bind_alarm_catalog     = bind_alarm_catalog,
        .validate               = validate,
        .init_adapters          = init_adapters,
        .register_runtime_tasks = register_runtime_tasks,
        .start_runtime          = start_runtime,
        .assert_safe_outputs    = assert_safe_outputs,
    };
    return bootstrap_register_hooks(&s_hooks);
}
