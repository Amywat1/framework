/**
 * @file    home_device.c
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "application/usecases/home_device.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"

sw_err_t home_device(void)
{
    return event_publish(EVT_CMD_HOME_DEVICE, 0U);
}
