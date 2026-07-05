/**
 * @file    hal_sensor_poll.c
 * @brief   HAL 传感器滤波周期任务注册
 * @author  HUWANGWEI
 * @date    2026-07-04
 */

#include "framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_poll.h"
#include "framework/ports/outbound/hal/hal_sensor_port.h"
#include "framework/runtime/periodic_task/periodic_task.h"
#include "framework/runtime/config/thread_config.h"

#include <sched.h>

#define HAL_SENSOR_POLL_PERIOD_MS  50U

static void hal_sensor_poll_task(void *ctx)
{
    const hal_sensor_ops_t *sensor;

    (void)ctx;

    sensor = hal_sensor_get_ops();
    if ((sensor != NULL) && (sensor->tick != NULL))
    {
        sensor->tick();
    }
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
