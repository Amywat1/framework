/**
 * @file    project_hooks_sim.c
 * @brief   Demo 仿真项目生命周期钩子
 */

#include "adapters/outbound/storage/json/json_deploy_store.h"
#include "adapters/outbound/storage/json/json_param_store.h"
#include "ports/port_contract.h"
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
    /* demo 用到的端口：IO 与语音仿真后端、参数与部署存储、机型操作，
     * 以及框架自身注册的命令入站与报警绑定。
     * 未用到的云端与方案加载不声明，因此不会被要求注册。 */
    return port_contract_validate(PORT_REQ_HAL_IO | PORT_REQ_HAL_VOICE | PORT_REQ_PARAM_STORE | PORT_REQ_DEPLOY_STORE
                                  | PORT_REQ_MACHINE_OPS | PORT_REQ_ALARM_BINDING | PORT_REQ_SAFETY);
}

static sw_err_t init_adapters(void)
{
    return SW_OK;
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
