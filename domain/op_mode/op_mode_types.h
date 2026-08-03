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
 *   INIT → STOPPED（初始化完成；上电默认运营总开关开启，可 RECOVER）
 *   STOPPED → HOMING（RECOVER 正常归位，需 service_enabled）→ IDLE / EXCEPTION
 *   IDLE → STOPPED（STOP_OPERATION：停运并关闭总开关）
 *   STOPPED → STOPPED（RESUME_OPERATION：仅重新授权，仍须 RECOVER 进 IDLE）
 *   IDLE → WASHING（START_WASH）
 *   WASHING → ABORT_HOMING（非急停中止清障）→ EXCEPTION
 *   WASHING → WASH_DONE（正常完成且无 MAJOR+）→ IDLE（客户离场）
 *   WASHING → EXCEPTION（正常完成但仍有 MAJOR+，洗后评估）
 *   WASH_DONE → STOPPED（STOP_OPERATION）
 *   STOPPED/EXCEPTION → SELF_CHECK → STOPPED / EXCEPTION
 *   EXCEPTION → RECOVERING（RECOVER 故障恢复）→ IDLE / EXCEPTION
 *   任意 → EXCEPTION（急停触发；LOCKOUT 在非洗车态）
 *
 * @note   不变量：IDLE 蕴含 service_enabled==true；关总开关时不得停留在 IDLE。
 *         静态状态 STOPPED/IDLE/WASH_DONE 收敛后不得存在 MAJOR 及以上活动告警；
 *         MINOR 告警可与正常状态共存。
 */
typedef enum {
    OP_MODE_INIT = 0,     /**< 系统初始化中（operational_mode_init 前）*/
    OP_MODE_STOPPED,      /**< 停机（未运营或待归位；总开关关时禁止 HOME）*/
    OP_MODE_HOMING,       /**< 归位中（STOPPED → IDLE）*/
    OP_MODE_IDLE,         /**< 运营待机（总开关必开，可接单）*/
    OP_MODE_WASHING,      /**< 洗车会话执行中 */
    OP_MODE_ABORT_HOMING, /**< 中止归位中（非急停洗车中止 → EXCEPTION）*/
    OP_MODE_WASH_DONE,    /**< 洗车完成，等待客户离场 */
    OP_MODE_SELF_CHECK,   /**< 自检中 */
    OP_MODE_EXCEPTION,    /**< 故障停机 */
    OP_MODE_RECOVERING,   /**< 恢复中（归位 + 阻塞告警验证）*/
} operational_mode_t;

/**
 * @brief  洗车模式选择子（不透明，取值与含义由项目定义）
 * @note   框架只传递与存储，不解释具体模式名；项目自行定义常量并映射到方案。
 */
typedef uint8_t wash_mode_t;

/** @brief  无效洗车模式（框架侧占位，项目勿用作合法模式） */
#define WASH_MODE_INVALID ((wash_mode_t)0xFFU)

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
    OP_REJECT_VEHICLE_NOT_READY, /**< 机型准入未就绪（抽象；具体条件由项目定义）*/
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
