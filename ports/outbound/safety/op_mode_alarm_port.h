/**
 * @file    op_mode_alarm_port.h
 * @brief   运行模式与报警交互端口（急停码识别）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef PORTS_OUTBOUND_SAFETY_OP_MODE_ALARM_PORT_H
#define PORTS_OUTBOUND_SAFETY_OP_MODE_ALARM_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  判断报警码是否为急停报警
 * @param  alarm_code  报警码
 * @retval true  急停报警
 * @retval false 其他报警
 * @note   项目层提供强符号实现；框架默认弱符号返回 false
 */
bool op_mode_alarm_port_is_estop(uint32_t alarm_code);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_OUTBOUND_SAFETY_OP_MODE_ALARM_PORT_H */
