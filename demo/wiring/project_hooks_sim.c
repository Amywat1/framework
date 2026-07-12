/**
 * @file    project_hooks_sim.c
 * @brief   Demo 仿真项目生命周期钩子
 */

#include "application/alarm_event_bridge.h"
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

sw_err_t project_hal_extra_setup(void)
{
    return SW_OK;
}

sw_err_t project_safety_init(void)
{
    return SW_OK;
}

sw_err_t project_machine_setup(void)
{
    return demo_machine_ops_register();
}

sw_err_t project_alarm_catalog_init(void)
{
    return demo_alarm_catalog_load();
}

sw_err_t project_report_scheduler_init(void)
{
    return SW_OK;
}

sw_err_t project_adapters_init(void)
{
    return SW_OK;
}

sw_err_t project_start_threads(void)
{
    return periodic_task_register("alarm_bridge", 50U, alarm_bridge_poll, NULL, SCHED_OTHER, 0, THD_SENSOR_POLL_STACK);
}

void project_assert_safe_outputs(void)
{
}
