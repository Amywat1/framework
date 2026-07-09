/**
 * @file    op_mode_bridge.h
 * @brief   运行模式事件桥接（替代 device_fsm）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef APPLICATION_OP_MODE_BRIDGE_H
#define APPLICATION_OP_MODE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

sw_err_t op_mode_bridge_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_OP_MODE_BRIDGE_H */
