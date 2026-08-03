/**
 * @file    command_types.h
 * @brief   设备命令类型定义（裁决结果 + 提交回执）
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#ifndef DOMAIN_OP_MODE_COMMAND_TYPES_H
#define DOMAIN_OP_MODE_COMMAND_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"

#include <stdint.h>

/* -------------------------------------------------------------------------
 * 裁决结果（command_decision）
 * ------------------------------------------------------------------------- */

/** @brief  待执行副作用种类 */
typedef enum {
    DEV_CMD_EFFECT_NONE = 0,
    DEV_CMD_EFFECT_START_WASH,
    DEV_CMD_EFFECT_STOP_WASH,
    DEV_CMD_EFFECT_SELF_CHECK,
    DEV_CMD_EFFECT_HOME_DEVICE,
    DEV_CMD_EFFECT_MANUAL_ACTUATOR,
    DEV_CMD_EFFECT_STOP_ALL_OUTPUTS,
} dev_cmd_effect_t;

/** @brief  命令裁决结果（含待执行副作用） */
typedef struct {
    op_cmd_result_t    verdict;
    op_reject_reason_t reason;
    dev_cmd_effect_t   pending_effect;
} dev_cmd_decision_t;

/* -------------------------------------------------------------------------
 * 提交回执（command_receipt）
 * ------------------------------------------------------------------------- */

/** @brief  命令处理状态 */
typedef enum {
    DEV_CMD_STATUS_ACCEPTED = 0,
    DEV_CMD_STATUS_REJECTED,
    DEV_CMD_STATUS_FAILED,
    DEV_CMD_STATUS_TIMEOUT,
    DEV_CMD_STATUS_BUSY,
} dev_cmd_status_t;

/** @brief  命令提交回执 */
typedef struct {
    dev_cmd_status_t   status;
    op_reject_reason_t reject_reason;
    sw_err_t           effect_error;
    uint64_t           request_id;
} dev_cmd_receipt_t;

/* -------------------------------------------------------------------------
 * EVT_OP_MODE_CMD_HANDLED 的 param 编解码
 *
 * 布局（低位在右）：[23:16]=cmd_kind  [15:8]=status  [7:0]=reject_reason
 * 与 op_mode_types.h 中其他事件同理，禁止在调用点手写移位。
 * ------------------------------------------------------------------------- */

/**
 * @brief  编码 EVT_OP_MODE_CMD_HANDLED 的 param
 * @param  cmd_kind  命令类别（dev_cmd_kind_t，以整型承载避免头文件循环依赖）
 * @param  status    处理状态
 * @param  reason    拒绝原因（非拒绝时为 OP_REJECT_NONE）
 */
static inline uint32_t cmd_handled_evt_param(uint8_t cmd_kind, dev_cmd_status_t status, op_reject_reason_t reason)
{
    return (((uint32_t)cmd_kind & OP_MODE_EVT_FIELD_MASK) << 16)
           | (((uint32_t)status & OP_MODE_EVT_FIELD_MASK) << 8) | ((uint32_t)reason & OP_MODE_EVT_FIELD_MASK);
}

/** @brief 从 EVT_OP_MODE_CMD_HANDLED 的 param 解出命令类别 */
static inline uint8_t cmd_handled_kind(uint32_t param)
{
    return (uint8_t)((param >> 16) & OP_MODE_EVT_FIELD_MASK);
}

/** @brief 从 EVT_OP_MODE_CMD_HANDLED 的 param 解出处理状态 */
static inline dev_cmd_status_t cmd_handled_status(uint32_t param)
{
    return (dev_cmd_status_t)((param >> 8) & OP_MODE_EVT_FIELD_MASK);
}

/** @brief 从 EVT_OP_MODE_CMD_HANDLED 的 param 解出拒绝原因 */
static inline op_reject_reason_t cmd_handled_reason(uint32_t param)
{
    return (op_reject_reason_t)(param & OP_MODE_EVT_FIELD_MASK);
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_OP_MODE_COMMAND_TYPES_H */
