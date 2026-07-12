/**
 * @file    command_decision.h
 * @brief   OperationalMode 命令裁决结果
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_COMMAND_GATEWAY_COMMAND_DECISION_H
#define DOMAIN_COMMAND_GATEWAY_COMMAND_DECISION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/command_gateway/op_mode_types.h"

/**
 * @brief  待执行副作用种类
 */
typedef enum {
    DEV_CMD_EFFECT_NONE = 0,
    DEV_CMD_EFFECT_START_WASH,
    DEV_CMD_EFFECT_STOP_WASH,
    DEV_CMD_EFFECT_SELF_CHECK,
    DEV_CMD_EFFECT_RESET_FAULT,
    DEV_CMD_EFFECT_HOME_DEVICE,
    DEV_CMD_EFFECT_MANUAL_ACTUATOR,
    DEV_CMD_EFFECT_STOP_ALL_OUTPUTS,
} dev_cmd_effect_t;

/**
 * @brief  命令裁决结果（含待执行副作用）
 */
typedef struct {
    op_cmd_result_t    verdict;
    op_reject_reason_t reason;
    dev_cmd_effect_t   pending_effect;
} dev_cmd_decision_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_COMMAND_GATEWAY_COMMAND_DECISION_H */
