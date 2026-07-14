/**
 * @file    project_hooks_sim.c
 * @brief   Demo 仿真项目生命周期钩子
 */

#include "application/alarm_event_bridge.h"
#include "adapters/outbound/storage/json/json_deploy_store.h"
#include "adapters/outbound/storage/json/json_param_store.h"
#include "runtime/bootstrap/project_hooks.h"
#include "runtime/config/thread_config.h"
#include "runtime/scheduler/periodic_task.h"

#include <sched.h>

extern sw_err_t demo_machine_ops_register(void);
extern sw_err_t demo_alarm_catalog_load(void);

static void alarm_bridge_poll(void *ctx)
{
    (void)ctx;
    alarm_event_bridge_drain();
}

sw_err_t project_configure_storage(void)
{
    sw_err_t r;

    r = json_param_store_configure(PARAM_STORE_JSON_FILE_PATH);
    if (r != SW_OK) {
        return r;
    }
    return json_deploy_store_configure(DEPLOY_STORE_JSON_FILE_PATH);
}

sw_err_t project_configure_hal(void)
{
    return SW_OK;
}

sw_err_t project_bind_hal(void)
{
    return SW_OK;
}

sw_err_t project_init_hal(void)
{
    return SW_OK;
}

sw_err_t project_configure_safety(void)
{
    return SW_OK;
}

sw_err_t project_configure_adapters(void)
{
    return SW_OK;
}

sw_err_t project_bind_machine(void)
{
    return demo_machine_ops_register();
}

sw_err_t project_bind_alarm_catalog(void)
{
    return demo_alarm_catalog_load();
}

sw_err_t project_validate(void)
{
    return SW_OK;
}

sw_err_t project_init_adapters(void)
{
    return SW_OK;
}

sw_err_t project_register_runtime_tasks(void)
{
    return periodic_task_register("alarm_bridge", 50U, alarm_bridge_poll, NULL, SCHED_OTHER, 0, THD_SENSOR_POLL_STACK);
}

sw_err_t project_start_runtime(void)
{
    return SW_OK;
}

void project_assert_safe_outputs(void)
{
}
