/**
 * @file    hal_sensor_poll.c
 * @brief   HAL 传感器滤波周期任务注册
 * @author  HUWANGWEI
 * @date    2026-07-04
 */

#include "framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_poll.h"
#include "framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter_internal.h"
#include "framework/runtime/scheduler/periodic_task.h"
#include "framework/runtime/config/thread_config.h"

#include <sched.h>

#define HAL_SENSOR_POLL_PERIOD_MS  50U

static void hal_sensor_poll_task(void *ctx)
{
    (void)ctx;
    (void)hal_sensor_filter_tick_once();
}

sw_err_t hal_sensor_poll_register_task(void)
{
    return periodic_task_register("hal_sensor_poll",
                                  HAL_SENSOR_POLL_PERIOD_MS,
                                  hal_sensor_poll_task,
                                  NULL,
                                  SCHED_OTHER,
                                  THD_SENSOR_POLL_NICE,
                                  THD_SENSOR_POLL_STACK);
}
