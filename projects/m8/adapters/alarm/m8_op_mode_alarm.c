/**
 * @file    m8_op_mode_alarm.c
 * @brief   M8 急停报警码识别
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/ports/outbound/safety/op_mode_alarm_port.h"
#include "framework/domain/safety/model/alarm_code.h"
#include "projects/m8/config/m8_alarm_table.h"

bool op_mode_alarm_port_is_estop(uint32_t alarm_code)
{
    return alarm_code == M8_ALARM_CODE_ESTOP;
}
