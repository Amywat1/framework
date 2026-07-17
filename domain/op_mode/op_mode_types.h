/**
 * @file    op_mode_types.h
 * @brief   运行模式状态机共享类型
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef DOMAIN_OP_MODE_OP_MODE_TYPES_H
#define DOMAIN_OP_MODE_OP_MODE_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* -------------------------------------------------------------------------
 * 整机运行模式
 * ------------------------------------------------------------------------- */

/**
 * @brief  整机运行模式（10 态状态机）
 *
 * 状态迁移概览：
 *   INIT → STOPPED（初始化完成）
 *   STOPPED → HOMING（HOME_DEVICE，需 service_enabled）→ IDLE / EXCEPTION
 *   IDLE → WASHING（START_WASH）
 *   WASHING → ALARM_HOMING（非急停中止）→ EXCEPTION
 *   WASHING → WASH_DONE（正常完成）→ IDLE（客户离场）
 *   STOPPED/EXCEPTION → SELF_CHECK → STOPPED / EXCEPTION
 *   EXCEPTION → RECOVERING（RECOVER）→ IDLE / EXCEPTION
 *   任意 → EXCEPTION（急停触发，或 LOCKOUT）
 */
typedef enum {
    OP_MODE_INIT = 0,        /**< 系统初始化中（operational_mode_init 前）*/
    OP_MODE_STOPPED,         /**< 停机（手动维护可用，归位后进入待机）*/
    OP_MODE_HOMING,          /**< 归位中（STOPPED → IDLE）*/
    OP_MODE_IDLE,            /**< 待机，等待洗车指令 */
    OP_MODE_WASHING,         /**< 洗车会话执行中 */
    OP_MODE_ALARM_HOMING,    /**< 报警归位中（WASHING 中止 → EXCEPTION）*/
    OP_MODE_WASH_DONE,       /**< 洗车完成，等待客户离场 */
    OP_MODE_SELF_CHECK,      /**< 自检中 */
    OP_MODE_EXCEPTION,       /**< 故障停机 */
    OP_MODE_RECOVERING,      /**< 恢复中（清告警 + 归位 + 验证）*/
} operational_mode_t;

/** @brief  洗车会话中止原因 */
typedef enum {
    WASH_ABORT_MANUAL = 0,   /**< 人工发出 STOP_WASH 指令 */
    WASH_ABORT_CRITICAL,     /**< 安全 LOCKOUT（报警触发）*/
    WASH_ABORT_STEP_TIMEOUT, /**< 洗车步骤超时 */
    WASH_ABORT_INTERNAL,     /**< 内部引擎错误 */
    WASH_ABORT_ESTOP,        /**< 硬件急停触发 */
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
    if (param <= (uint32_t)WASH_ABORT_ESTOP) {
        return (wash_abort_cause_t)param;
    }
    return WASH_ABORT_INTERNAL;
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_OP_MODE_OP_MODE_TYPES_H */
