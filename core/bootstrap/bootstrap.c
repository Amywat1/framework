/**
 * @file    bootstrap.c
 * @brief   系统完整启动序列实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "core/bootstrap/bootstrap.h"
#include "core/bootstrap/wiring.h"
#include "core/event_bus/event_bus.h"
#include "core/scheduler/thread_registry.h"
#include "core/scheduler/scheduler.h"
#include "service/svc_param/svc_param.h"
#include "service/dev_ctx/dev_ctx.h"
#include "domain/device/actuator/motor/motor.h"
#include "domain/device/unit/brush.h"
#include "domain/device/unit/gantry.h"
#include "domain/device/water.h"
#include "application/orchestrators/emergency_handler.h"
#include "application/orchestrators/device_fsm.h"
#include "application/orchestrators/wash_orchestrator.h"
#include "application/orchestrators/report_aggregator.h"
#include "application/orchestrators/safety_supervisor.h"
#include "domain/safety/alarm_core.h"
#include "domain/safety/safety_fsm.h"
#include "adapters/storage/json/alarm_catalog_json.h"
#include "adapters/machine/m8/m8_sensor.h"
#include "adapters/machine/m8/m8_alarm_adapt.h"
#include "adapters/machine/m8/m8_water_setup.h"
#include "adapters/machine/m8/m8_motor_setup.h"
#ifdef BUILD_SIM
#  include "adapters/machine/m8/m8_signal_sim.h"
#endif
#include "ports/hal/hal_io_port.h"
#include "ports/hal/hal_vfd_port.h"
#include "ports/storage/deploy_store.h"
#include "config/threading/thread_config.h"
#include "common/time_util.h"
#include "common/log.h"
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>

#ifndef BUILD_SIM
#  include "adapters/machine/m8/m8_boot_profile.h"
#  include "adapters/machine/m8/m8_vfd_setup.h"
#  include "adapters/machine/m8/m8_cli_setup.h"
#  include "adapters/cloud/aliyun/aliyun_command_adapter.h"
#  include "common/event_types.h"
#endif

/* 报警目录 JSON 路径：sim 由 CMake 注入绝对路径；真机默认部署路径
 * （与引擎配置同属尚未统一的部署路径问题，先占位）。*/
#ifndef M8_ALARM_CONFIG_PATH
#  define M8_ALARM_CONFIG_PATH "/etc/m8/m8_alarm_catalog.json"
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

    {
        const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

        if ((vfd == NULL) || (vfd->init == NULL))
        {
            LOG_ERROR("bootstrap: hal_vfd ops not registered");
            return SW_ERR_NOT_INIT;
        }
        BOOT_CHECK(vfd->init(), "hal_vfd_init");
    }

#ifndef BUILD_SIM
    BOOT_CHECK(m8_vfd_setup(), "m8_vfd_setup");
#endif
    BOOT_CHECK(m8_motor_setup(), "m8_motor_setup");

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
#ifndef BUILD_SIM
    BOOT_CHECK(m8_boot_profile_init(), "m8_boot_profile_init");
#endif

    BOOT_CHECK(m8_sensor_setup(), "m8_sensor_setup");
#ifdef BUILD_SIM
    m8_signal_sim_reset_all();
#endif
    BOOT_CHECK(m8_sensor_warmup(), "m8_sensor_warmup");
    return SW_OK;
}

/** 设备域与应用编排 */
static sw_err_t bootstrap_init_application(void)
{
    BOOT_CHECK(motor_init(),             "motor_init");
    BOOT_CHECK(brush_init(),             "brush_init");
    BOOT_CHECK(gantry_init(),            "gantry_init");
    BOOT_CHECK(m8_water_setup(),         "m8_water_setup");
    /* 安全/报警域：alarm_core 先装兜底目录+注册端口，加载 JSON 目录整表替换，
     * 再依次接好状态机、监督器、机型适配 */
    BOOT_CHECK(alarm_core_init(),        "alarm_core_init");
    {
        /* static：避免在启动栈上放下 ALARM_CATALOG_MAX 条定义；启动期单线程安全 */
        static alarm_def_t s_alarm_defs[ALARM_CATALOG_MAX];
        unsigned cnt = 0U;
        char     cerr[160];
        sw_err_t lr  = alarm_catalog_load_json_file(M8_ALARM_CONFIG_PATH,
                                                    s_alarm_defs, ALARM_CATALOG_MAX,
                                                    &cnt, cerr, sizeof(cerr));
        if (lr == SW_OK)
        {
            (void)alarm_core_load(s_alarm_defs, cnt);
        }
        else
        {
            LOG_ERROR("bootstrap: alarm catalog 加载失败(%s)，保留内置兜底目录", cerr);
        }
    }
    BOOT_CHECK(safety_fsm_init(),        "safety_fsm_init");
    BOOT_CHECK(safety_supervisor_init(), "safety_supervisor_init");
    BOOT_CHECK(m8_alarm_adapt_init(),    "m8_alarm_adapt_init");
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
    if (aliyun_command_adapter_init())
    {
        (void)event_publish(EVT_CLOUD_CONNECTED, 0U);
    }
    m8_cli_setup();
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

    BOOT_CHECK(m8_sensor_poll_register(), "register io_poll_thread");

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
