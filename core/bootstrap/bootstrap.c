/**
 * @file    bootstrap.c
 * @brief   系统完整启动序列实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "core/bootstrap/bootstrap.h"
#include "core/bootstrap/wiring.h"
#include "core/event_bus/event_bus.h"
#include "core/scheduler/thread_registry.h"
#include "core/scheduler/scheduler.h"
#include "service/svc_param/svc_param.h"
#include "service/dev_ctx/dev_ctx.h"
#include "domain/safety/alarm_core.h"
#include "domain/safety/safety_fsm.h"
#include "domain/device/actuator/motor/motor.h"
#include "domain/device/unit/brush.h"
#include "domain/device/unit/gantry.h"
#include "domain/device/water.h"
#include "domain/device/gate.h"
#include "application/orchestrators/emergency_handler.h"
#include "application/orchestrators/device_fsm.h"
#include "application/orchestrators/wash_orchestrator.h"
#include "application/orchestrators/report_aggregator.h"
#include "adapters/machine/m8/m8_alarm_adapt.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "adapters/machine/m8/m8_io_poll.h"
#include "ports/hal/hal_io_port.h"
#include "ports/storage/deploy_store.h"
#include "config/threading/thread_config.h"
#include "common/time_util.h"
#include "common/log.h"
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>

#ifndef BUILD_SIM
#  include "adapters/machine/m8/m8_boot_profile.h"
#  include "adapters/hal/linux_hw/m8_hal_ctx.h"
extern void aliyun_command_adapter_init(void);
extern void cli_adapter_init(void);
#endif

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

#ifndef BUILD_SIM
    m8_assert_safe_outputs();
#endif

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
#ifndef BUILD_SIM
        if (io->register_panic_cb != NULL)
        {
            io->register_panic_cb(m8_assert_safe_outputs);
        }
#endif
    }

#ifndef BUILD_SIM
    BOOT_CHECK(m8_linux_hw_init(), "m8_linux_hw_init");
#endif

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

/** 硬件安全态与报警链路 */
static sw_err_t bootstrap_init_safety(void)
{
#ifndef BUILD_SIM
    BOOT_CHECK(m8_boot_profile_init(), "m8_boot_profile_init");
#endif

    BOOT_CHECK(alarm_core_init(), "alarm_core_init");
    m8_signal_filter_init();
    BOOT_CHECK(m8_alarm_adapt_init(), "m8_alarm_adapt_init");
    BOOT_CHECK(safety_fsm_init(), "safety_fsm_init");
    return SW_OK;
}

/** 设备域与应用编排 */
static sw_err_t bootstrap_init_application(void)
{
    BOOT_CHECK(motor_init(),             "motor_init");
    BOOT_CHECK(brush_init(),             "brush_init");
    BOOT_CHECK(gantry_init(),            "gantry_init");
    BOOT_CHECK(water_init(),             "water_init");
    BOOT_CHECK(gate_init(),              "gate_init");
    BOOT_CHECK(emergency_handler_init(), "emergency_handler_init");
    BOOT_CHECK(device_fsm_init(),        "device_fsm_init");
    BOOT_CHECK(wash_orchestrator_init(), "wash_orchestrator_init");
    BOOT_CHECK(report_aggregator_init(), "report_aggregator_init");
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

#ifndef BUILD_SIM
    aliyun_command_adapter_init();
    cli_adapter_init();
#endif

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

    BOOT_CHECK(m8_io_poll_register(), "register io_poll_thread");

    BOOT_CHECK(thread_register("motor_tick",
                               motor_tick_loop,
                               SCHED_OTHER, 0,
                               THD_MOTOR_TICK_STACK),
               "register motor_tick_thread");

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
