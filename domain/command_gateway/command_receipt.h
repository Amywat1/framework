/**
 * @file    command_receipt.h
 * @brief   设备命令提交回执
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_COMMAND_GATEWAY_COMMAND_RECEIPT_H
#define DOMAIN_COMMAND_GATEWAY_COMMAND_RECEIPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/command_gateway/op_mode_types.h"

#include <stdint.h>

/**
 * @brief  命令处理状态
 */
typedef enum {
    DEV_CMD_STATUS_ACCEPTED = 0,
    DEV_CMD_STATUS_REJECTED,
    DEV_CMD_STATUS_FAILED,
    DEV_CMD_STATUS_TIMEOUT,
    DEV_CMD_STATUS_BUSY,
} dev_cmd_status_t;

/**
 * @brief  命令提交回执
 */
typedef struct {
    dev_cmd_status_t   status;
    op_reject_reason_t reject_reason;
    sw_err_t           effect_error;
    uint64_t           request_id;
} dev_cmd_receipt_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_COMMAND_GATEWAY_COMMAND_RECEIPT_H */
