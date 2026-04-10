/**
 * @file    bootstrap.c
 * @brief   系统完整启动序列实现
 * @author  胡望伟
 * @date    2026-04-10
 *
 * 初始化顺序（严格，每步失败则中止）：
 *   1.  time_util_init()            — 时间戳基准，event_bus 入队依赖
 *   2.  event_bus_init()            — 事件总线，后续所有模块可发布/订阅
 *   3.  wiring()                    — 注册所有 port→adapter（依赖注入）
 *   4.  svc_param_init()            — 加载持久化参数（允许文件缺失）
 *   5.  dev_ctx_init()              — 设备状态快照清零
 *   6.  m8_boot_profile_init()      — IO 子板就绪等待 + 输出安全态
 *   7.  alarm_core_init()           — 报警引擎清零
 *   8.  m8_alarm_adapt_init()       — 注册 M8 IO 轮询和急停复位回调
 *   9.  safety_fsm_init()           — 安全状态机（订阅报警事件）
 *   10. domain/device init          — brush/gantry/top_lift/water/gate
 *   11. safety_supervisor_init()    — 安全监督者（订阅安全事件）
 *   12. device_fsm_init()           — 设备 FSM（订阅命令/安全/流程事件）
 *   13. wash_orchestrator_init()    — 洗车编排器（注册 worker_thread）
 *   14. report_aggregator_init()    — 上报聚合器（注册 cloud_thread）
 *   15. 注册 event_dispatch_thread
 *   16. 注册 io_poll_thread
 *   17. scheduler_start_all()       — 创建所有线程
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
#include "adapters/machine/m8/m8_boot_profile.h"  /* 声明 m8_boot_profile_init */
#include "config/threading/thread_config.h"
#include "common/time_util.h"
#include "common/log.h"
#include <unistd.h>
#include <sched.h>

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

    /* 3. 依赖注入 */
    BOOT_CHECK(wiring(), "wiring");

    /* 4. 参数管理（允许文件缺失，降级使用默认值）*/
    {
        sw_err_t r = svc_param_init();
        if ((r != SW_OK) && (r != SW_ERR_STORAGE))
        {
            LOG_ERROR("bootstrap: svc_param_init failed ret=%d", (int)r);
            return r;
        }
    }

    /* 5. 设备状态快照 */
    BOOT_CHECK(dev_ctx_init(), "dev_ctx_init");

    /* 6. 上电安全初始化（IO 子板就绪 + 输出安全态）*/
    BOOT_CHECK(m8_boot_profile_init(), "m8_boot_profile_init");

    /* 7. 报警引擎 */
    BOOT_CHECK(alarm_core_init(), "alarm_core_init");

    /* 8. 注册 M8 报警适配回调 */
    BOOT_CHECK(m8_alarm_adapt_init(), "m8_alarm_adapt_init");

    /* 9. 安全状态机 */
    BOOT_CHECK(safety_fsm_init(), "safety_fsm_init");

    /* 10. 设备组件 */
    BOOT_CHECK(brush_init(),    "brush_init");
    BOOT_CHECK(gantry_init(),   "gantry_init");
    BOOT_CHECK(top_lift_init(), "top_lift_init");
    BOOT_CHECK(water_init(),    "water_init");
    BOOT_CHECK(gate_init(),     "gate_init");

    /* 11. 安全监督者 */
    BOOT_CHECK(safety_supervisor_init(), "safety_supervisor_init");

    /* 12. 设备 FSM */
    BOOT_CHECK(device_fsm_init(), "device_fsm_init");

    /* 13. 洗车编排器 */
    BOOT_CHECK(wash_orchestrator_init(), "wash_orchestrator_init");

    /* 14. 上报聚合器 */
    BOOT_CHECK(report_aggregator_init(), "report_aggregator_init");

    /* 15. 注册 event_dispatch_thread */
    BOOT_CHECK(thread_register("event_dispatch",
                               event_dispatch_thread_fn,
                               SCHED_OTHER, 0,
                               THD_EVENT_DISPATCH_STACK),
               "register event_dispatch_thread");

    /* 16. 注册 io_poll_thread */
    BOOT_CHECK(thread_register("io_poll",
                               io_poll_thread_fn,
                               SCHED_OTHER, 0,
                               THD_IO_POLL_STACK),
               "register io_poll_thread");

    /* 17. 启动所有线程 */
    BOOT_CHECK(scheduler_start_all(), "scheduler_start_all");

    LOG_INFO("bootstrap: system started successfully");
    return SW_OK;
}
