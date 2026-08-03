/**
 * @file    safety_thread.c
 * @brief   EStop 硬件快速通道实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "runtime/platform/safety_thread.h"

#include "common/event_types.h"
#include "common/log.h"
#include "ports/outbound/safety/hw_estop_port.h"
#include "ports/outbound/safety/safety_cutout_port.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/thread_registry.h"

#include <sched.h>
#include <unistd.h>

/**
 * @brief  处理一次急停边沿（上升/下降沿）
 */
static void handle_estop_edge(bool active)
{
    if (active) {
        safety_cutout_execute();
        (void)event_publish(EVT_HW_ESTOP_ON, 0U);
        LOG_WARN("safety_thread: HW ESTOP ON");
    } else {
        (void)event_publish(EVT_HW_ESTOP_OFF, 0U);
        LOG_INFO("safety_thread: HW ESTOP OFF");
    }
}

/*
 * 生命周期：与 periodic_task 一致，注册后不可停止。
 * 线程以 pthread_detach 创建，进程退出即随之终止；不提供 stop 接口，
 * 以免在急停热路径上引入额外判断和可被误用的关闭时序。
 */
static void *safety_thread_fn(void *arg)
{
    bool last_active = false;
    bool initialized = false;

    (void)arg;

    for (;;) {
        bool active = hw_estop_port_is_active();

        if (!initialized) {
            last_active = active;
            initialized = true;
            if (active) {
                handle_estop_edge(true);
            }
        } else if (active != last_active) {
            last_active = active;
            handle_estop_edge(active);
        }

        usleep((unsigned long)THD_SAFETY_THREAD_POLL_US);
    }

    /* 不可达：上方循环无退出条件，此处仅为满足非 void 返回类型 */
    return NULL;
}

sw_err_t safety_thread_init(void)
{
    return thread_register(
        "safety_thread", safety_thread_fn, SCHED_FIFO, THD_SAFETY_THREAD_PRIO, THD_SAFETY_THREAD_STACK);
}
