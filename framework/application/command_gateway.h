/**
 * @file    command_gateway.h
 * @brief   外部命令网关（RPC 到 event_dispatch 线程）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef APPLICATION_COMMAND_GATEWAY_H
#define APPLICATION_COMMAND_GATEWAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  初始化命令网关并注册 command_port
 */
sw_err_t command_gateway_init(void);

/**
 * @brief  在 event_dispatch 线程处理待执行命令
 */
void command_gateway_drain(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_COMMAND_GATEWAY_H */
