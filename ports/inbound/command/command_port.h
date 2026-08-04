/**
 * @file    command_port.h
 * @brief   外部命令接入端口接口（MQTT 下行 / CLI / BLE 等统一提交点）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    各命令来源解析为 dev_cmd_t 后通过 device_command_port.submit() 提交；
 *          command_gateway 在 event_dispatch 线程仲裁并执行副作用。
 */

#ifndef PORTS_INBOUND_COMMAND_COMMAND_PORT_H
#define PORTS_INBOUND_COMMAND_COMMAND_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/op_mode/command_types.h"
#include "domain/op_mode/device_command.h"

/**
 * @brief  设备命令入站端口操作表
 */
typedef struct {
    /**
     * @brief  提交一条外部命令（同步等待裁决与副作用，带超时）
     * @param  cmd         已解析命令
     * @param  receipt     输出回执；可为 NULL（丢弃细节）
     * @param  timeout_ms  等待超时（毫秒）；0 使用网关默认值
     * @retval SW_OK       已写入 receipt（含 REJECTED/FAILED 等业务结果）
     * @retval SW_ERR_STATE 在 event dispatch 线程内调用（禁止，见下方约束）
     * @retval SW_ERR_*    队列/硬件错误（未进入业务裁决）
     *
     * @par 调用上下文约束：
     *   必须从 event dispatch 线程**以外**的线程调用。裁决与副作用在
     *   dispatch 线程执行，调用方线程阻塞等待其完成；若从 dispatch 线程
     *   自身调用，wake 事件会排在队列中永远等不到处理，直至超时。
     *   命令网关会拦截此类调用并直接返回 SW_ERR_STATE。
     */
    sw_err_t (*submit)(const dev_cmd_t *cmd, dev_cmd_receipt_t *receipt, uint32_t timeout_ms);
} device_command_port_ops_t;

sw_err_t                         device_command_port_register(const device_command_port_ops_t *ops);
const device_command_port_ops_t *device_command_port_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_INBOUND_COMMAND_COMMAND_PORT_H */
