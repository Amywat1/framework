/**
 * @file    op_mode_alarm_port.c
 * @brief   急停码识别默认实现（弱符号，项目层可覆盖）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/ports/outbound/safety/op_mode_alarm_port.h"

__attribute__((weak)) bool op_mode_alarm_port_is_estop(uint32_t alarm_code)
{
    (void)alarm_code;
    return false;
}
