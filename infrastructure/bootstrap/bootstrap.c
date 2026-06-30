/**
 * @file    bootstrap.c
 * @brief   系统完整启动序列实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "infrastructure/bootstrap/bootstrap.h"
#include "infrastructure/bootstrap/wiring.h"
#include "infrastructure/event_bus/event_bus.h"
#include "infrastructure/scheduler/thread_registry.h"
#include "infrastructure/scheduler/scheduler.h"
#include "infrastructure/services/svc_param/svc_param.h"
#include "infrastructure/services/dev_ctx/dev_ctx.h"
#include "domain/device/actuator/motor/motor.h"
#include "domain/device/water.h"
#include "application/orchestrators/emergency_handler.h"
#include "application/orchestrators/device_fsm.h"
#include "application/orchestrators/wash_orchestrator.h"
#include "application/orchestrators/report_aggregator.h"
#include "application/orchestrators/safety_supervisor.h"
#include "domain/safety/alarm_core.h"
#include "domain/safety/safety_fsm.h"
#include "machines/m8/adapters/setup/m8_sensor.h"
#include "machines/m8/adapters/alarm/m8_alarm_init.h"
#include "machines/m8/adapters/alarm/m8_alarm_adapt.h"
#include "machines/m8/adapters/alarm/m8_comm_watchdog.h"
#include "machines/m8/adapters/setup/m8_water_setup.h"
#include "machines/m8/adapters/setup/m8_motor_setup.h"
#include "machines/m8/adapters/setup/m8_brush_setup.h"
#include "machines/m8/adapters/setup/m8_gantry_setup.h"
#ifdef BUILD_SIM
#  include "machines/m8/adapters/m8_signal_sim.h"
#endif
#include "ports/hal/hal_io_port.h"
#include "ports/hal/hal_vfd_port.h"
#include "ports/hal/hal_voice_port.h"
#include "ports/storage/deploy_store.h"
#include "config/threading/thread_config.h"
#include "common/time_util.h"
#include "common/log.h"
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>

#ifndef BUILD_SIM
#  include "machines/m8/adapters/m8_boot_profile.h"
#  include "machines/m8/adapters/setup/m8_vfd_setup.h"
#  include "machines/m8/adapters/setup/m8_voice_setup.h"
#  include "machines/m8/adapters/cli/m8_cli_setup.h"
#  include "adapters/cloud/aliyun/aliyun_adapter.h"
#  include "machines/m8/adapters/cloud/mqtt_command_parser.h"
#  include "common/event_types.h"
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

    {
        const hal_voice_ops_t *voice = hal_voice_get_ops();

        if ((voice == NULL) || (voice->init == NULL))
        {
            LOG_ERROR("bootstrap: hal_voice ops not registered");
            return SW_ERR_NOT_INIT;
        }
        BOOT_CHECK(voice->init(), "hal_voice_init");
    }

#ifndef BUILD_SIM
    BOOT_CHECK(m8_vfd_setup(),   "m8_vfd_setup");
    BOOT_CHECK(m8_voice_setup(), "m8_voice_setup");
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
    BOOT_CHECK(m8_brush_setup(),         "m8_brush_setup");
    BOOT_CHECK(m8_gantry_setup(),        "m8_gantry_setup");
    BOOT_CHECK(m8_water_setup(),         "m8_water_setup");
    /* 安全/报警域初始化顺序：
     *   ① alarm_core_init()        注册 alarm_binding_port，目录初始为空
     *   ② safety_fsm_init()        订阅 EVT_ALARM_*
     *   ③ safety_supervisor_init() 订阅 EVT_SAFETY_* / EVT_ALARM_*
     *   ④ m8_alarm_init()          合并三张表一次注入完整目录（须在 poll 前完成）
     *   ⑤ m8_alarm_adapt_init()    DI 防抖预热（目录已就绪）
     *   ⑥ m8_comm_watchdog_init()  心跳时间戳初始化（给设备 timeout_ms 窗口首次通讯）*/
    BOOT_CHECK(alarm_core_init(),          "alarm_core_init");
    BOOT_CHECK(safety_fsm_init(),          "safety_fsm_init");
    BOOT_CHECK(safety_supervisor_init(),   "safety_supervisor_init");
    BOOT_CHECK(m8_alarm_init(),            "m8_alarm_init");
    BOOT_CHECK(m8_alarm_adapt_init(),      "m8_alarm_adapt_init");
    BOOT_CHECK(m8_comm_watchdog_init(),    "m8_comm_watchdog_init");
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
    if (aliyun_command_adapter_init(m8_mqtt_command_parse))
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

    BOOT_CHECK(m8_sensor_poll_start(), "m8_sensor_poll_start");
    BOOT_CHECK(m8_alarm_adapt_poll_start(), "m8_alarm_adapt_poll_start");

    BOOT_CHECK(motor_tick_start(), "motor_tick_start");

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
