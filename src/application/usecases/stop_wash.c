/**
 * @file    stop_wash.c
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "application/usecases/stop_wash.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"

sw_err_t stop_wash(void)
{
    return event_publish(EVT_CMD_STOP_WASH, 0U);
}

sw_err_t stop_operation(void)
{
    return event_publish(EVT_CMD_STOP_OPERATION, 0U);
}
