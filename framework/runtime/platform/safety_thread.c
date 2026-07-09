/**
 * @file    safety_thread.c
 * @brief   EStop 硬件快速通道实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/runtime/platform/safety_thread.h"
#include "framework/runtime/platform/device_safety_actuator.h"
#include "framework/ports/outbound/safety/hw_estop_port.h"
#include "framework/runtime/scheduler/thread_registry.h"
#include "framework/runtime/config/thread_config.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"

#include <stdatomic.h>
#include <unistd.h>
#include <sched.h>

static atomic_bool s_terminate = false;

/**
 * @brief  处理一次急停边沿（上升/下降沿）
 */
static void handle_estop_edge(bool active)
{
    if (active)
    {
        device_stop_all_actuators();
        (void)event_publish(EVT_HW_ESTOP_ON, 0U);
        LOG_WARN("safety_thread: HW ESTOP ON");
    }
    else
    {
        (void)event_publish(EVT_HW_ESTOP_OFF, 0U);
        LOG_INFO("safety_thread: HW ESTOP OFF");
    }
}

static void *safety_thread_fn(void *arg)
{
    bool last_active = false;
    bool initialized = false;

    (void)arg;

    while (!atomic_load(&s_terminate))
    {
        bool active = hw_estop_port_is_active();

        if (!initialized)
        {
            last_active = active;
            initialized = true;
            if (active)
            {
                handle_estop_edge(true);
            }
        }
        else if (active != last_active)
        {
            last_active = active;
            handle_estop_edge(active);
        }

        usleep((unsigned long)THD_SAFETY_THREAD_POLL_US);
    }

    return NULL;
}

sw_err_t safety_thread_init(void)
{
    atomic_store(&s_terminate, false);

    return thread_register("safety_thread",
                           safety_thread_fn,
                           SCHED_FIFO,
                           THD_SAFETY_THREAD_PRIO,
                           THD_SAFETY_THREAD_STACK);
}
