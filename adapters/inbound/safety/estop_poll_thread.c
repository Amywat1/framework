/**
 * @file    estop_poll_thread.c
 * @brief   急停轮询采集线程实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "adapters/inbound/safety/estop_poll_thread.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/ports/outbound/safety/safety_port.h"
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
        /* 切断失败不在此重试：重试会延长动力输出未确认切断的窗口，且失败原因
         * 通常在硬件链路本身（总线离线、板卡无响应），重试无从改变。此处只保证
         * 失败被记录，并且失败不阻断后续事件发布——EVT_HW_ESTOP_ON 必须照常送出，
         * 否则领域层不会进入急停态，故障会同时丢掉切断与状态收敛两条路径。 */
        sw_err_t cut_ret = safety_cutout_execute();

        (void)event_publish(EVT_HW_ESTOP_ON, 0U);
        if (cut_ret != SW_OK) {
            LOG_ERROR("estop_poll: HW ESTOP ON，但切断未确认完成 ret=%d", (int)cut_ret);
        } else {
            LOG_WARN("estop_poll: HW ESTOP ON");
        }
    } else {
        (void)event_publish(EVT_HW_ESTOP_OFF, 0U);
        LOG_INFO("estop_poll: HW ESTOP OFF");
    }
}

/*
 * 生命周期：与 periodic_task 一致，注册后不可停止。
 * 线程以 pthread_detach 创建，进程退出即随之终止；不提供 stop 接口，
 * 以免在急停热路径上引入额外判断和可被误用的关闭时序。
 */
static void *estop_poll_thread_fn(void *arg)
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

sw_err_t estop_poll_thread_init(void)
{
    return thread_register(
        "estop_poll", estop_poll_thread_fn, SCHED_FIFO, THD_SAFETY_THREAD_PRIO, THD_SAFETY_THREAD_STACK);
}
