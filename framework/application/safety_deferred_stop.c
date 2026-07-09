/**
 * @file    safety_deferred_stop.c
 * @brief   急停延后完备停机默认实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/application/safety_deferred_stop.h"
#include "framework/domain/device_control/mechanism/brush.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/device_control/mechanism/water.h"

__attribute__((weak)) void safety_deferred_stop(void)
{
    (void)gantry_stop();
    (void)brush_stop_all();
    (void)water_all_off();
}
