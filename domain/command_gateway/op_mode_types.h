/**
 * @file    op_mode_types.h
 * @brief   运行模式状态机共享类型
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef DOMAIN_COMMAND_GATEWAY_OP_MODE_TYPES_H
#define DOMAIN_COMMAND_GATEWAY_OP_MODE_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/command_gateway/device_command.h"
#include "domain/device_control/model/device_state.h"

#include <stdint.h>

/** @brief  洗车会话中止原因 */
typedef enum {
    WASH_ABORT_MANUAL = 0,
    WASH_ABORT_CRITICAL,
    WASH_ABORT_STEP_TIMEOUT,
    WASH_ABORT_INTERNAL,
} wash_abort_cause_t;

/** @brief  命令仲裁结果 */
typedef enum {
    OP_CMD_ALLOWED = 0,
    OP_CMD_DENIED,
} op_cmd_result_t;

/** @brief  命令拒绝原因 */
typedef enum {
    OP_REJECT_NONE = 0,
    OP_REJECT_WRONG_MODE,
    OP_REJECT_ESTOP_ACTIVE,
    OP_REJECT_SERVICE_DISABLED,
    OP_REJECT_UNKNOWN_CMD,
} op_reject_reason_t;

/** @brief  命令裁决与拒绝原因（完整 pipeline 使用 dev_cmd_decision_t） */
typedef struct {
    op_cmd_result_t    result;
    op_reject_reason_t reason;
} op_command_result_t;

/** @brief  恢复流程结束结果 */
typedef enum {
    RECOVERY_RESULT_IDLE = 0,
    RECOVERY_RESULT_EXCEPTION,
} recovery_result_t;

/**
 * @brief  将中止原因编码为 EVT_WASH_ABORTED 的 param
 */
static inline uint32_t wash_abort_evt_param(wash_abort_cause_t cause)
{
    return (uint32_t)cause;
}

/**
 * @brief  从 EVT_WASH_ABORTED 的 param 解码中止原因
 */
static inline wash_abort_cause_t wash_abort_from_evt_param(uint32_t param)
{
    if (param <= (uint32_t)WASH_ABORT_INTERNAL) {
        return (wash_abort_cause_t)param;
    }
    return WASH_ABORT_INTERNAL;
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_COMMAND_GATEWAY_OP_MODE_TYPES_H */
