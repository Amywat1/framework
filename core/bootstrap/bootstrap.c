/**
 * @file    bootstrap.c
 * @brief   系统完整启动序列实现
 * @author  胡望伟
 * @date    2026-04-10
 *
 * 初始化顺序（严格，每步失败则中止）：
 *   1.  time_util_init()            — 时间戳基准，event_bus 入队依赖
 *   2.  event_bus_init()            — 事件总线，后续所有模块可发布/订阅
 *   3.  drv_io_init()               — IO 子板 CAN 驱动（仅初始化数据，线程由 scheduler 统一创建）
 *   4.  wiring()                    — port→adapter 依赖注入（仅做注册，不做硬件初始化）
 *   5.  m8_linux_hw_init()          — M8 硬件上下文初始化（VFD Modbus + 步进驱动，依赖 drv_io）
 *   6.  svc_param_init()            — 加载持久化参数（允许文件缺失，降级默认值）
 *   7.  dev_ctx_init()              — 设备状态快照清零
 *   8.  m8_boot_profile_init()      — 等待 IO 子板就绪 + 所有 DO 置安全态
 *   9.  alarm_core_init()           — 报警引擎清零
 *   10. m8_alarm_adapt_init()       — 注册 M8 IO 轮询和急停复位回调
 *   11. safety_fsm_init()           — 安全状态机（订阅报警事件）
 *   12. domain/device init          — brush/gantry/top_lift/water/gate
 *   13. safety_supervisor_init()    — 安全监督者（订阅安全事件）
 *   14. device_fsm_init()           — 设备 FSM（订阅命令/安全/流程事件）
 *   15. wash_orchestrator_init()    — 洗车编排器（注册 worker_thread）
 *   16. report_aggregator_init()    — 上报聚合器（注册 cloud_thread）
 *   17. deploy_store_load()         — 加载部署配置（SN、MQTT 凭证）
 *   18. aliyun_command_adapter_init() — 连接 MQTT，注册命令接收回调
 *   19. cli_adapter_init()          — 注册 CLI 命令域（device/safety/param/diag）
 *   20. 注册 event_dispatch_thread
 *   21. 注册 io_rw_thread
 *   22. 注册 io_poll_thread
 *   23. scheduler_start_all()       — 创建所有线程
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
#include "domain/device/brush.h"
#include "domain/device/gantry.h"
#include "domain/device/top_lift.h"
#include "domain/device/water.h"
#include "domain/device/gate.h"
#include "application/orchestrators/safety_supervisor.h"
#include "application/orchestrators/device_fsm.h"
#include "application/orchestrators/wash_orchestrator.h"
#include "application/orchestrators/report_aggregator.h"
#include "adapters/machine/m8/m8_alarm_adapt.h"
#include "ports/storage/deploy_store.h"
#include "config/threading/thread_config.h"
#include "common/time_util.h"
#include "common/log.h"
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>

/* 真机专属头文件（仿真构建不依赖这些）*/
#ifndef BUILD_SIM
#  include "adapters/machine/m8/m8_boot_profile.h"
#  include "adapters/hal/linux_hw/m8_hal_ctx.h"
#  include "driver/drv_io.h"
#endif

/* 阶段六适配器：声明为 extern，避免包含 snack SDK 头文件 */
#ifndef BUILD_SIM
extern void aliyun_command_adapter_init(void);
extern void cli_adapter_init(void);
#endif

/* -------------------------------------------------------------------------
 * 进程级 panic handler（event_bus fatal 回调）
 *
 * 约定：本函数必须终止进程，不可 return。
 * 由 systemd Restart=on-failure 负责重启，重启后系统从安全态重新初始化。
 * ------------------------------------------------------------------------- */
static void system_panic_safe_stop(event_bus_fatal_reason_t reason, int sys_errno)
{
    LOG_ERROR("PANIC: event_bus fatal reason=%d errno=%d, asserting safe outputs and aborting",
              (int)reason, sys_errno);

#ifndef BUILD_SIM
    /* 最佳努力：直接写寄存器将所有 DO 置安全态，不依赖 event_bus */
    m8_assert_safe_outputs();
#endif

    abort(); /* 产生 core dump，触发 systemd Restart=on-failure */
}

/* -------------------------------------------------------------------------
 * 线程入口函数（bootstrap 本地，不对外暴露）
 * ------------------------------------------------------------------------- */

/** event_dispatch_thread：永久阻塞在 event_bus_dispatch_loop */
static void *event_dispatch_thread_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop(); /* 永不返回 */
    return NULL;
}

/** io_poll_thread：每 ALARM_POLL_PERIOD_MS 调用一次 alarm_core_tick_ms */
static void *io_poll_thread_fn(void *arg)
{
    (void)arg;
    while (true)
    {
        alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
        usleep((unsigned long)ALARM_POLL_PERIOD_MS * 1000UL);
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * 宏：初始化步骤失败则打印并返回
 * ------------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------
 * 启动序列
 * ------------------------------------------------------------------------- */
sw_err_t bootstrap_run(void)
{
    /* 1. 时间戳基准 */
    time_util_init();

    /* 2. 事件总线 */
    BOOT_CHECK(event_bus_init(), "event_bus_init");

    /* 2a. 注册 fatal 回调（须在 dispatch 线程启动前完成）*/
    event_bus_set_fatal_cb(system_panic_safe_stop);

#ifndef BUILD_SIM
    /* 3. IO 子板 CAN 驱动（仅初始化数据，线程由 scheduler 统一创建；仿真跳过）*/
    BOOT_CHECK(drv_io_init(), "drv_io_init");

    /* 3a. 注入全板离线安全停机回调 */
    drv_io_register_panic_cb(m8_assert_safe_outputs);
#endif

    /* 4. 依赖注入：注册所有 port→adapter（纯注册，无硬件操作）*/
    BOOT_CHECK(wiring(), "wiring");

#ifndef BUILD_SIM
    /* 5. M8 硬件上下文：VFD Modbus 通道 + 步进驱动（仿真跳过）*/
    BOOT_CHECK(m8_linux_hw_init(), "m8_linux_hw_init");
#endif

    /* 6. 参数管理（允许文件缺失，降级使用默认值）*/
    {
        sw_err_t r = svc_param_init();
        if ((r != SW_OK) && (r != SW_ERR_STORAGE))
        {
            LOG_ERROR("bootstrap: svc_param_init failed ret=%d", (int)r);
            return r;
        }
    }

    /* 7. 设备状态快照 */
    BOOT_CHECK(dev_ctx_init(), "dev_ctx_init");

#ifndef BUILD_SIM
    /* 8. 上电安全初始化（等待 IO 子板就绪 + 所有 DO 置安全态；仿真跳过）*/
    BOOT_CHECK(m8_boot_profile_init(), "m8_boot_profile_init");
#endif

    /* 9. 报警引擎 */
    BOOT_CHECK(alarm_core_init(), "alarm_core_init");

    /* 10. 注册 M8 报警适配回调 */
    BOOT_CHECK(m8_alarm_adapt_init(), "m8_alarm_adapt_init");

    /* 11. 安全状态机 */
    BOOT_CHECK(safety_fsm_init(), "safety_fsm_init");

    /* 12. 设备组件 */
    BOOT_CHECK(brush_init(),    "brush_init");
    BOOT_CHECK(gantry_init(),   "gantry_init");
    BOOT_CHECK(top_lift_init(), "top_lift_init");
    BOOT_CHECK(water_init(),    "water_init");
    BOOT_CHECK(gate_init(),     "gate_init");

    /* 13. 安全监督者 */
    BOOT_CHECK(safety_supervisor_init(), "safety_supervisor_init");

    /* 14. 设备 FSM */
    BOOT_CHECK(device_fsm_init(), "device_fsm_init");

    /* 15. 洗车编排器 */
    BOOT_CHECK(wash_orchestrator_init(), "wash_orchestrator_init");

    /* 16. 上报聚合器 */
    BOOT_CHECK(report_aggregator_init(), "report_aggregator_init");

    /* 17. 加载部署配置（SN、MQTT 凭证；允许文件缺失）*/
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
    }

#ifndef BUILD_SIM
    /* 18. 连接 MQTT + 注册命令接收回调（失败不中止；仿真跳过）*/
    aliyun_command_adapter_init();

    /* 19. 注册 CLI 命令域（仿真跳过，避免 snack SDK 依赖）*/
    cli_adapter_init();
#endif

    /* 20. 注册 event_dispatch_thread */
    BOOT_CHECK(thread_register("event_dispatch",
                               event_dispatch_thread_fn,
                               SCHED_OTHER, 0,
                               THD_EVENT_DISPATCH_STACK),
               "register event_dispatch_thread");

#ifndef BUILD_SIM
    /* 21. 注册 io_rw_thread */
    BOOT_CHECK(thread_register("io_rw",
                               drv_io_poll_loop,
                               SCHED_OTHER, 0,
                               THD_IO_RW_STACK),
               "register io_rw_thread");
#endif

    /* 22. 注册 io_poll_thread */
    BOOT_CHECK(thread_register("io_poll",
                               io_poll_thread_fn,
                               SCHED_OTHER, 0,
                               THD_IO_POLL_STACK),
               "register io_poll_thread");

    /* 23. 启动所有线程 */
    BOOT_CHECK(scheduler_start_all(), "scheduler_start_all");

    LOG_INFO("bootstrap: system started successfully");
    return SW_OK;
}
