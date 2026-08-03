/**
 * @file    safety_weak_defaults.c
 * @brief   安全快速通道与急停接口弱符号默认实现
 *
 * @note    四个弱符号均可由项目层强符号覆盖：
 *          - safety_cutout_execute：立即切断动力输出（safety_thread 热路径）
 *          - hw_estop_port_is_active：读取硬件急停输入
 *          - op_mode_alarm_port_is_estop：识别急停报警码
 *          - safety_deferred_stop：急停后延后完备停机
 */

#include "ports/outbound/safety/hw_estop_port.h"
#include "ports/outbound/safety/op_mode_alarm_port.h"
#include "ports/outbound/safety/safety_cutout_port.h"
#include "ports/outbound/safety/safety_deferred_stop.h"

#include <stdbool.h>
#include <stdint.h>

__attribute__((weak)) void safety_cutout_execute(void)
{
}

__attribute__((weak)) bool hw_estop_port_is_active(void)
{
    return false;
}

__attribute__((weak)) bool op_mode_alarm_port_is_estop(uint32_t alarm_code)
{
    (void)alarm_code;
    return false;
}

__attribute__((weak)) void safety_deferred_stop(void)
{
}
