/**
 * @file    m8_io_poll.c
 * @brief   M8 IO 轮询线程实现
 * @author  胡望伟
 * @date    2026-06-01
 */

#include "adapters/machine/m8/m8_io_poll.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "core/scheduler/thread_registry.h"
#include "domain/safety/alarm_core.h"
#include "ports/hal/hal_indicator_port.h"
#include "config/threading/thread_config.h"
#include <sched.h>
#include <unistd.h>

static void *io_poll_thread_fn(void *arg)
{
    (void)arg;

    while (true)
    {
        m8_signal_filter_tick();

        {
            const hal_indicator_ops_t *indicator = hal_indicator_get_ops();
            if ((indicator != NULL) && (indicator->entry_light_tick != NULL))
            {
                indicator->entry_light_tick();
            }
        }

        alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
        usleep((unsigned long)ALARM_POLL_PERIOD_MS * 1000UL);
    }

    return NULL;
}

sw_err_t m8_io_poll_register(void)
{
    return thread_register("io_poll",
                           io_poll_thread_fn,
                           SCHED_OTHER,
                           0,
                           THD_IO_POLL_STACK);
}
