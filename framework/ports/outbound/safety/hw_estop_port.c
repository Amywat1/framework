/**
 * @file    hw_estop_port.c
 * @brief   硬件急停输入端口默认弱实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/ports/outbound/safety/hw_estop_port.h"

__attribute__((weak)) bool hw_estop_port_is_active(void)
{
    return false;
}
