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

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_OP_MODE_COMMAND_TYPES_H */
