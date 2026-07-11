/**
 * @file    device_safety_actuator.c
 * @brief   设备级安全停机入口（委托 safety_cutout_execute）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "runtime/platform/device_safety_actuator.h"
#include "ports/outbound/safety/safety_cutout_port.h"

void device_stop_all_actuators(void)
{
    safety_cutout_execute();
}
