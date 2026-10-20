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
 * @brief  整机运行模式（8 态；不表达同时在洗车数）
 *
 * 状态迁移概览：
 *   INIT → STOPPED（初始化完成；上电默认运营总开关开启，可 RECOVER）
 *   STOPPED → RECOVERING（RECOVER：安全触发门禁 + 清非安全报警 + 异步归位）→ STANDBY / STOPPED
 *   任意非 INIT → STOPPED（STOP_ALL_OUTPUTS：切断输出，清空订单簿，不改总开关）
 *   任意非 INIT → 同态（SET_SERVICE：仅翻总开关；关闭时若在 STANDBY/HOLD 则落到 STOPPED）
 *   STANDBY / ACTIVE 可 SUBMIT_ORDER 入簿；放行后占用 0→1 进入 ACTIVE
 *   ACTIVE → CLEARING（CLEAR_MACHINE：停放行、中止在洗、项目清障）→ STOPPED
 *   ACTIVE → HOLD 或 STANDBY（最后一单结束；HOLD 由 profile.hold_after_last_job 决定）
 *   HOLD → STANDBY（场区清空）
 *   STOPPED → SELF_CHECK → STOPPED
 *   急停解除不自动进 STANDBY；故障条件由 estop / blocking / LOCKOUT 旗标表达
 *
 * @note   不变量：STANDBY 蕴含 service_enabled==true 且无急停。
 *         MINOR 告警可与正常状态共存。占用数见 active_jobs，不占用独立模式。
 */
typedef enum {
    OP_MODE_INIT = 0,     /**< 系统初始化中（operational_mode_init 前）*/
    OP_MODE_STOPPED,      /**< 停机（未运营或待归位；可带或不带故障旗标）*/
    OP_MODE_STANDBY,      /**< 运营待机（总开关开且无急停，可接单放行）*/
    OP_MODE_ACTIVE,       /**< 至少一单在洗 */
    OP_MODE_CLEARING,     /**< 整机清障/排空中 */
    OP_MODE_HOLD,         /**< 最后一单结束，等待场区清空 */
    OP_MODE_SELF_CHECK,   /**< 自检中 */
    OP_MODE_RECOVERING,   /**< 恢复中（归位 + 阻塞告警验证）*/
} operational_mode_t;

/**
 * @brief  洗车模式选择子（不透明，取值与含义由项目定义）
 * @note   框架只传递与存储，不解释具体模式名；项目自行定义常量并映射到方案。
 */
typedef uint8_t wash_mode_t;

/** @brief  无效洗车模式（框架侧占位，项目勿用作合法模式） */
#define WASH_MODE_INVALID ((wash_mode_t)0xFFU)

/** @brief  作业中止原因 */
typedef enum {
    WASH_ABORT_MANUAL = 0,   /**< 人工发出 CLEAR_MACHINE */
    WASH_ABORT_CRITICAL,     /**< 安全 LOCKOUT（报警触发）*/
    WASH_ABORT_STEP_TIMEOUT, /**< 步骤超时 */
    WASH_ABORT_INTERNAL,     /**< 内部引擎错误 */
    WASH_ABORT_ESTOP,        /**< 硬件急停触发 */
    WASH_ABORT_STOP_ALL,     /**< 手动 STOP_ALL_OUTPUTS：全切断且不清障 */
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
    OP_REJECT_ENTRY_NOT_READY,    /**< 机型准入未就绪（抽象；具体条件由项目定义）*/
    OP_REJECT_UNKNOWN_CMD,
    OP_REJECT_CUTOUT_UNCONFIRMED, /**< 安全切断失败且尚无独立反馈确认 */
} op_reject_reason_t;

/** @brief  恢复流程结束结果 */
typedef enum {
    RECOVERY_RESULT_STANDBY = 0, /**< 归位成功且当时无禁止接单报警 → STANDBY */
    RECOVERY_RESULT_FAILED,      /**< 安全触发挡起步、清故障超时、归位失败或归位后仍阻断 → STOPPED */
} recovery_result_t;

/**
 * @brief  将中止原因编码为 32 位事件参数（安全路径 / 测试辅助）
 */
static inline uint32_t wash_abort_evt_param(wash_abort_cause_t cause)
{
    return (uint32_t)cause;
}

/**
 * @brief  从 32 位事件参数解码中止原因
 * @note   EVT_JOB_ABORTED 的 param 现为 job_id，不得用本函数解读。
 */
static inline wash_abort_cause_t wash_abort_from_evt_param(uint32_t param)
{
    if (param <= (uint32_t)WASH_ABORT_STOP_ALL) {
        return (wash_abort_cause_t)param;
    }
    return WASH_ABORT_INTERNAL;
}

/* -------------------------------------------------------------------------
 * 事件 param 编解码
 *
 * event_t.param 是单个 uint32_t，多字段事件靠位段承载。发布方与订阅方若各自
 * 手写移位，字段顺序或宽度调整时编译器无法发现不一致，只会表现为运行期取到
 * 错位的值。因此所有多字段 param 一律经这里的 encode/decode 收口，禁止在
 * 调用点手写 `<<` / `&`。
 *
 * 布局（低位在右）：
 *   EVT_OP_MODE_CHANGED  [15:8]=from      [7:0]=to
 *   EVT_JOB_STARTED      [7:0]=wash_mode
 *   命令完成见 command_types.h 的 EVT_OP_MODE_CMD_HANDLED 编解码
 * ------------------------------------------------------------------------- */

/** @brief 单字段位宽掩码 */
#define OP_MODE_EVT_FIELD_MASK 0xFFU

/**
 * @brief  编码 EVT_OP_MODE_CHANGED 的 param
 * @param  from  变更前模式
 * @param  to    变更后模式
 */
static inline uint32_t op_mode_changed_evt_param(operational_mode_t from, operational_mode_t to)
{
    return (((uint32_t)from & OP_MODE_EVT_FIELD_MASK) << 8) | ((uint32_t)to & OP_MODE_EVT_FIELD_MASK);
}

/** @brief 从 EVT_OP_MODE_CHANGED 的 param 解出变更前模式 */
static inline operational_mode_t op_mode_changed_from(uint32_t param)
{
    return (operational_mode_t)((param >> 8) & OP_MODE_EVT_FIELD_MASK);
}

/** @brief 从 EVT_OP_MODE_CHANGED 的 param 解出变更后模式 */
static inline operational_mode_t op_mode_changed_to(uint32_t param)
{
    return (operational_mode_t)(param & OP_MODE_EVT_FIELD_MASK);
}

/** @brief 编码 EVT_JOB_STARTED 的 param */
static inline uint32_t job_started_evt_param(wash_mode_t mode)
{
    return (uint32_t)mode & OP_MODE_EVT_FIELD_MASK;
}

/** @brief 从 EVT_JOB_STARTED 的 param 解出洗车模式 */
static inline wash_mode_t job_started_mode(uint32_t param)
{
    return (wash_mode_t)(param & OP_MODE_EVT_FIELD_MASK);
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_OP_MODE_OP_MODE_TYPES_H */
