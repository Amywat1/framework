/**
 * @file    m8_vfd_tick.c
 * @brief   M8 VFD periodic runtime task registration.
 */

#include "projects/m8/bindings/m8_vfd_tick.h"

#include "framework/ports/outbound/hal/hal_vfd_port.h"
#include "framework/runtime/config/thread_config.h"
#include "framework/runtime/periodic_task/periodic_task.h"

#include <sched.h>

#define M8_VFD_TICK_PERIOD_MS 20U

static void m8_vfd_tick(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if ((vfd != NULL) && (vfd->tick != NULL))
    {
        vfd->tick();
    }
}

sw_err_t m8_vfd_tick_register_task(void)
{
    return periodic_task_register("vfd_tick",
                                  M8_VFD_TICK_PERIOD_MS,
                                  m8_vfd_tick,
                                  NULL,
                                  SCHED_OTHER,
                                  THD_VFD_TICK_NICE,
                                  THD_VFD_TICK_STACK);
}
