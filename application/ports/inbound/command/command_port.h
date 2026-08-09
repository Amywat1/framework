/**
 * @file    command_port.h
 * @brief   外部命令接入端口接口（MQTT 下行 / CLI / BLE 等统一提交点）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    各命令来源解析为 dev_cmd_t 后经本端口提交；command_gateway 在
 *          独立 cmd_control 线程裁决并执行副作用。主路径为 submit_async；
 *          submit_sync 仅为「入队 + 阻塞等待完成」的适配。
 */

#ifndef APPLICATION_PORTS_INBOUND_COMMAND_COMMAND_PORT_H
#define APPLICATION_PORTS_INBOUND_COMMAND_COMMAND_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/op_mode/command_types.h"
#include "domain/op_mode/device_command.h"

#include <stdint.h>

/**
 * @brief  设备命令入站端口操作表
 */
typedef struct {
    /**
     * @brief  异步提交一条外部命令（入队即返回）
     * @param  cmd         已解析命令
     * @param  request_id  输出请求 ID；可为 NULL
     * @retval SW_OK       已入队；结果见 EVT_OP_MODE_CMD_HANDLED（trace.command_id）
     * @retval SW_ERR_*    参数/队列/事件错误（未进入业务裁决）
     *
     * @note   可从非 drain 线程调用；亦可在副作用触发的同轮 drain 扫描中入队。
     *         完成通知不经过调用方线程阻塞。
     */
    sw_err_t (*submit_async)(const dev_cmd_t *cmd, uint64_t *request_id);

    /**
     * @brief  同步提交一条外部命令（入队后阻塞等待裁决与副作用）
     * @param  cmd         已解析命令
     * @param  receipt     输出回执；可为 NULL（丢弃细节）
     * @param  timeout_ms  等待超时（毫秒）；0 使用网关默认值
     * @retval SW_OK       已写入 receipt（含 REJECTED/FAILED 等业务结果）
     * @retval SW_ERR_STATE 在 cmd_control 线程内调用（禁止，见下方约束）
     * @retval SW_ERR_*    队列/超时/硬件错误（未完成或未进入业务裁决）
     *
     * @par 调用上下文约束：
     *   必须从 cmd_control 线程**以外**的线程调用。内部语义等价于
     *   submit_async + 等待该 request 完成；若从 control 线程自身调用会自锁，
     *   命令网关拦截并返回 SW_ERR_STATE。
     */
    sw_err_t (*submit_sync)(const dev_cmd_t *cmd, dev_cmd_receipt_t *receipt, uint32_t timeout_ms);
} device_command_port_ops_t;

sw_err_t                         device_command_port_register(const device_command_port_ops_t *ops);
const device_command_port_ops_t *device_command_port_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_PORTS_INBOUND_COMMAND_COMMAND_PORT_H */
