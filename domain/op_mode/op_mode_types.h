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
 * @brief  整机运行模式（8 态状态机）
 *
 * 状态迁移概览：
 *   INIT → STOPPED（初始化完成；上电默认运营总开关开启，可 RECOVER）
 *   STOPPED → RECOVERING（RECOVER：复位锁存告警 + 异步归位）→ IDLE / STOPPED
 *   IDLE / WASH_DONE / STOPPED → STOPPED（STOP_OPERATION：停运并关闭总开关）
 *   STOPPED → STOPPED（RESUME_OPERATION：任意非 INIT 态均可；仅重新授权，仍须 RECOVER 进 IDLE）
 *   IDLE → WASHING（START_WASH）
 *   WASHING → ABORT_HOMING（STOP_WASH 等非急停/非全停中止清障）→ STOPPED
 *   WASHING → WASH_DONE（正常完成且无 MAJOR+）→ IDLE（客户离场）
 *   WASHING → STOPPED（正常完成但仍有 MAJOR+；或急停 / STOP_ALL，不清障）
 *   WASH_DONE → STOPPED（STOP_ALL / 告警 / 急停）
 *   STOPPED → SELF_CHECK → STOPPED（仅急停激活时拒绝启动自检；自检中不可 STOP_OPERATION）
 *   急停解除不自动进 IDLE；故障条件由 estop / blocking / LOCKOUT 旗标表达，不占用独立模式
 *
 * @note   不变量：IDLE 蕴含 service_enabled==true；关总开关时不得停留在 IDLE。
 *         IDLE/WASH_DONE 不得长期残留 MAJOR+ / LOCKOUT / 急停；出现时收敛到 STOPPED。
 *         MINOR 告警可与正常状态共存。故障态由安全快照表达，不设独立故障模式。
 *         运营归位走 RECOVERING；中止清障走 ABORT_HOMING——不再保留独立 HOMING 态。
 */
typedef enum {
    OP_MODE_INIT = 0,     /**< 系统初始化中（operational_mode_init 前）*/
    OP_MODE_STOPPED,      /**< 停机（未运营或待归位；可带或不带故障旗标）*/
    OP_MODE_IDLE,         /**< 运营待机（总开关必开，可接单）*/
    OP_MODE_WASHING,      /**< 洗车会话执行中 */
    OP_MODE_ABORT_HOMING, /**< 中止归位中（停洗清障 → STOPPED）*/
    OP_MODE_WASH_DONE,    /**< 洗车完成，等待客户离场 */
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

/** @brief  洗车会话中止原因 */
typedef enum {
    WASH_ABORT_MANUAL = 0,   /**< 人工发出 STOP_WASH 指令 */
    WASH_ABORT_CRITICAL,     /**< 安全 LOCKOUT（报警触发）*/
    WASH_ABORT_STEP_TIMEOUT, /**< 洗车步骤超时 */
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
    OP_REJECT_VEHICLE_NOT_READY, /**< 机型准入未就绪（抽象；具体条件由项目定义）*/
    OP_REJECT_UNKNOWN_CMD,
} op_reject_reason_t;

/** @brief  恢复流程结束结果 */
typedef enum {
    RECOVERY_RESULT_IDLE = 0, /**< 归位成功且无阻塞告警 → IDLE */
    RECOVERY_RESULT_FAILED,   /**< 归位失败或仍有阻塞 → STOPPED */
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
 *   EVT_OP_MODE_CHANGED       [15:8]=from      [7:0]=to
 *   EVT_WASH_SESSION_STARTED  [7:0]=wash_mode
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

/** @brief 编码 EVT_WASH_SESSION_STARTED 的 param */
static inline uint32_t wash_session_started_evt_param(wash_mode_t mode)
{
    return (uint32_t)mode & OP_MODE_EVT_FIELD_MASK;
}

/** @brief 从 EVT_WASH_SESSION_STARTED 的 param 解出洗车模式 */
static inline wash_mode_t wash_session_started_mode(uint32_t param)
{
    return (wash_mode_t)(param & OP_MODE_EVT_FIELD_MASK);
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_OP_MODE_OP_MODE_TYPES_H */
