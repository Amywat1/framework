/**
 * @file    bootstrap.c
 * @brief   系统完整启动序列实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/runtime/bootstrap/bootstrap.h"
#include "framework/runtime/bootstrap/wiring.h"
#include "framework/runtime/bootstrap/project_hooks.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/runtime/scheduler/thread_registry.h"
#include "framework/runtime/scheduler/scheduler.h"
#include "framework/services/param/svc_param.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/application/orchestrators/emergency_handler.h"
#include "framework/application/command_gateway.h"
#include "framework/application/op_mode_bridge.h"
#include "framework/application/recovery_service.h"
#include "framework/application/self_check_service.h"
#include "framework/application/mode_projection.h"
#include "framework/domain/command_gateway/operational_mode.h"
#include "framework/application/orchestrators/wash_orchestrator.h"
#include "framework/application/orchestrators/report_scheduler.h"
#include "framework/application/orchestrators/safety_supervisor.h"
#include "framework/runtime/platform/safety_thread.h"
#include "framework/application/alarm_event_bridge.h"
#include "framework/domain/safety/alarm_registry/alarm_registry.h"
#include "framework/domain/safety/safety_posture/safety_posture.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/ports/outbound/hal/hal_vfd_port.h"
#include "framework/ports/outbound/hal/hal_voice_port.h"
#include "framework/ports/outbound/storage/deploy_store.h"
#include "framework/runtime/config/thread_config.h"
#include "framework/common/time_util.h"
#include "framework/common/log.h"
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>

#define BOOT_CHECK(call, msg)                               \
    do {                                                    \
        sw_err_t _r = (call);                               \
        if (_r != SW_OK)                                    \
        {                                                   \
            LOG_ERROR("bootstrap: " msg " failed ret=%d",  \
                      (int)_r);                             \
            return _r;                                      \
        }                                                   \
    } while (0)

static void system_panic_safe_stop(event_bus_fatal_reason_t reason, int sys_errno)
{
    LOG_ERROR("PANIC: event_bus fatal reason=%d errno=%d, asserting safe outputs and aborting",
              (int)reason, sys_errno);

    project_assert_safe_outputs();

    abort();
}

static void *event_dispatch_thread_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

/** 基础设施：时间、事件总线、依赖注入、参数、dev_ctx */
static sw_err_t bootstrap_init_infra(void)
{
    time_util_init();

    BOOT_CHECK(event_bus_init(), "event_bus_init");
    event_bus_set_fatal_cb(system_panic_safe_stop);

    BOOT_CHECK(wiring(), "wiring");

    {
        const hal_io_ops_t *io = hal_io_get_ops();

        if ((io == NULL) || (io->init == NULL))
        {
            LOG_ERROR("bootstrap: hal_io ops not registered");
            return SW_ERR_NOT_INIT;
        }
        BOOT_CHECK(io->init(), "hal_io_init");
        if (io->register_panic_cb != NULL)
        {
            io->register_panic_cb(project_assert_safe_outputs);
        }
    }

    {
        const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

        if ((vfd == NULL) || (vfd->init == NULL))
        {
            LOG_ERROR("bootstrap: hal_vfd ops not registered");
            return SW_ERR_NOT_INIT;
        }
        BOOT_CHECK(vfd->init(), "hal_vfd_init");
    }

    {
        const hal_voice_ops_t *voice = hal_voice_get_ops();

        if ((voice == NULL) || (voice->init == NULL))
        {
            LOG_ERROR("bootstrap: hal_voice ops not registered");
            return SW_ERR_NOT_INIT;
        }
        BOOT_CHECK(voice->init(), "hal_voice_init");
    }

    BOOT_CHECK(project_hal_extra_setup(), "project_hal_extra_setup");
    {
        sw_err_t r = svc_param_init();
        if ((r != SW_OK) && (r != SW_ERR_STORAGE))
        {
            LOG_ERROR("bootstrap: svc_param_init failed ret=%d", (int)r);
            return r;
        }
    }

    BOOT_CHECK(dev_ctx_init(), "dev_ctx_init");
    return SW_OK;
}

/** 传感器初始化 */
static sw_err_t bootstrap_init_safety(void)
{
    BOOT_CHECK(project_safety_init(), "project_safety_init");
    return SW_OK;
}

/** 设备域与应用编排 */
static sw_err_t bootstrap_init_application(void)
{
    BOOT_CHECK(project_machine_setup(), "project_machine_setup");
    /* 安全/报警域初始化顺序：
     *   ① alarm_registry_init()
     *   ② safety_posture_init()
     *   ③ safety_supervisor_init()
     *   ④ project_alarm_catalog_init()
     *   ⑤ alarm_event_bridge_init()
     */
    BOOT_CHECK(alarm_registry_init(),          "alarm_registry_init");
    BOOT_CHECK(safety_posture_init(),          "safety_posture_init");
    BOOT_CHECK(safety_supervisor_init(),       "safety_supervisor_init");
    BOOT_CHECK(project_alarm_catalog_init(),   "project_alarm_catalog_init");
    BOOT_CHECK(alarm_event_bridge_init(),      "alarm_event_bridge_init");
    BOOT_CHECK(emergency_handler_init(),   "emergency_handler_init");
    BOOT_CHECK(safety_thread_init(),       "safety_thread_init");
    BOOT_CHECK(operational_mode_init(),    "operational_mode_init");
    BOOT_CHECK(command_gateway_init(),     "command_gateway_init");
    BOOT_CHECK(recovery_service_init(),    "recovery_service_init");
    BOOT_CHECK(self_check_service_init(),  "self_check_service_init");
    BOOT_CHECK(op_mode_bridge_init(),      "op_mode_bridge_init");
    BOOT_CHECK(mode_projection_init(),     "mode_projection_init");
    BOOT_CHECK(wash_orchestrator_init(),   "wash_orchestrator_init");
    BOOT_CHECK(project_report_scheduler_init(), "project_report_scheduler_init");
    return SW_OK;
}

/** 云端/CLI 适配与部署配置 */
static sw_err_t bootstrap_init_adapters(void)
{
    const deploy_store_ops_t *ds = deploy_store_get_ops();

    if (ds != NULL)
    {
        sw_err_t r = ds->load();
        if ((r != SW_OK) && (r != SW_ERR_STORAGE))
        {
            LOG_ERROR("bootstrap: deploy_store load failed ret=%d", (int)r);
            return r;
        }
    }

    BOOT_CHECK(project_adapters_init(), "project_adapters_init");
    return SW_OK;
}

/** 注册并启动所有线程 */
static sw_err_t bootstrap_start_threads(void)
{
    BOOT_CHECK(thread_register("event_dispatch",
                               event_dispatch_thread_fn,
                               SCHED_OTHER, 0,
                               THD_EVENT_DISPATCH_STACK),
               "register event_dispatch_thread");

#ifndef BUILD_SIM
    {
        const hal_io_ops_t *io = hal_io_get_ops();

        if ((io != NULL) && (io->start != NULL))
        {
            BOOT_CHECK(io->start(), "hal_io_start");
        }
    }
#endif

    BOOT_CHECK(project_start_threads(), "project_start_threads");

    BOOT_CHECK(scheduler_start_all(), "scheduler_start_all");
    return SW_OK;
}

sw_err_t bootstrap_run(void)
{
    sw_err_t ret;

    ret = bootstrap_init_infra();
    if (ret != SW_OK)
    {
        return ret;
    }

    ret = bootstrap_init_safety();
    if (ret != SW_OK)
    {
        return ret;
    }

    ret = bootstrap_init_application();
    if (ret != SW_OK)
    {
        return ret;
    }

    ret = bootstrap_init_adapters();
    if (ret != SW_OK)
    {
        return ret;
    }

    ret = bootstrap_start_threads();
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("bootstrap: system started successfully");
    return SW_OK;
}
