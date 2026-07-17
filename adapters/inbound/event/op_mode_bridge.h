/**
 * @file    op_mode_bridge.h
 * @brief   运行模式事件桥接（异步事件 → operational_mode 钩子）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef APPLICATION_OP_MODE_BRIDGE_H
#define APPLICATION_OP_MODE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化运行模式事件桥接并注册订阅表
 * @retval SW_OK 订阅成功
 */
sw_err_t op_mode_bridge_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_OP_MODE_BRIDGE_H */
