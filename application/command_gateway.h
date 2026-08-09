/**
 * @file    command_gateway.h
 * @brief   外部命令网关（独立 cmd_control 线程串行裁决）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef APPLICATION_COMMAND_GATEWAY_H
#define APPLICATION_COMMAND_GATEWAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化命令网关并注册 device_command_port；生产路径登记 cmd_control 线程
 */
sw_err_t command_gateway_init(void);

/**
 * @brief  在 cmd_control 线程处理待执行命令（STOP_ALL 优先）
 */
void command_gateway_drain(void);

#ifdef WDF_UNIT_TEST
/**
 * @brief  单元测试专用：启动 cmd_control 线程（生产由 scheduler_start_all 创建）
 */
sw_err_t command_gateway_start_control_for_test(void);

/**
 * @brief  单元测试专用：停止并 join cmd_control 线程
 */
void command_gateway_stop_control_for_test(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_COMMAND_GATEWAY_H */
