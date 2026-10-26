/**
 * @file    command_submit.h
 * @brief   入站命令提交 helper：补全追踪、判空端口、按默认超时同步提交
 *
 * @note    不含产品策略。拒入队、空队补单、本机观测由项目包一层。
 *          未注册 device_command_port 时返回 SW_ERR_NOT_INIT。
 */

#ifndef APPLICATION_COMMAND_SUBMIT_H
#define APPLICATION_COMMAND_SUBMIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "common/trace_context.h"
#include "domain/op_mode/command_types.h"
#include "domain/op_mode/device_command.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief 同步提交默认超时（毫秒） */
#define COMMAND_SUBMIT_TIMEOUT_MS 5000U

/**
 * @brief  补全 request_id / 会话 / 关联字段，并把当前追踪切到这条命令
 * @param  cmd       待提交命令；不可为空
 * @param  previous  写入调用前追踪，供结束后恢复；不可为空
 * @note   cmd 或 previous 为空则直接返回。request_id 为 0 时分配非 0。
 */
void command_submit_prepare(dev_cmd_t *cmd, trace_context_t *previous);

/**
 * @brief  经 device_command_port 同步提交；不改追踪上下文
 * @param  cmd      已准备好的命令；不可为空
 * @param  receipt  回执；可为 NULL
 * @retval SW_OK            端口已调用（业务结果见 receipt）
 * @retval SW_ERR_PARAM     cmd 为空
 * @retval SW_ERR_NOT_INIT  端口未注册或 submit_sync 为空
 * @retval 其它             端口返回值
 */
sw_err_t command_submit_dispatch(dev_cmd_t *cmd, dev_cmd_receipt_t *receipt);

/**
 * @brief  准备、同步提交并恢复追踪
 * @param  cmd 待提交命令
 * @retval SW_ERR_PARAM  cmd 为空
 * @retval 其它          同 command_submit_dispatch
 */
sw_err_t command_submit(dev_cmd_t *cmd);

/**
 * @brief  提交无载荷设备命令
 */
sw_err_t command_submit_kind(dev_cmd_kind_t kind, dev_cmd_source_t source);

/**
 * @brief  提交手动点动命令
 */
sw_err_t command_submit_manual(uint32_t act_id, int32_t param, dev_cmd_source_t source);

/**
 * @brief  提交运营总开关设置
 * @param  enabled  true 开启；false 关闭
 * @param  source   命令来源
 */
sw_err_t command_submit_set_service(bool enabled, dev_cmd_source_t source);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_COMMAND_SUBMIT_H */
