/**
 * @file    hw_estop_port.h
 * @brief   硬件急停输入端口（safety_thread 快速通道专用）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    须读取原始 DI，不走传感器滤波防抖链，以保证 ≤5ms 级响应。
 *          项目层提供强符号实现；默认弱符号返回 false。
 */

#ifndef PORTS_HW_ESTOP_PORT_H
#define PORTS_HW_ESTOP_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief  读取硬件急停是否处于激活（按下/断电）状态
 * @retval true   急停激活
 * @retval false  急停未激活或端口未实现
 */
bool hw_estop_port_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HW_ESTOP_PORT_H */
