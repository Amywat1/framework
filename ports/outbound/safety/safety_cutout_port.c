/**
 * @file    safety_cutout_port.c
 * @brief   硬件急停快速切断默认弱实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "ports/outbound/safety/safety_cutout_port.h"

__attribute__((weak)) void safety_cutout_execute(void)
{
}
