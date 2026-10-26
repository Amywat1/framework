/**
 * @file    safety_matrix.h
 * @brief   安全行为矩阵：报警等级与生命周期的语义收口
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    解决的问题：等级与生命周期的语义原先散落在判定点上——
 *          `level >= ALARM_LEVEL_MAJOR` 出现在 3 处、`level == CRITICAL` 出现在
 *          2 处、结案判定出现在多处。每处都在重新表达"MAJOR 意味着什么"，
 *          新增等级或调整语义时要逐个找齐，漏改不会有任何编译期提示。
 *
 *          做法：把"某等级具备哪些行为属性"做成一张表，判定点改为查表。
 *          这不是为了抽象，而是让安全语义可以一眼评审、并按格点测试。
 *
 * @note    本文件只描述 domain 内可判定的语义。命令允许矩阵依赖运行模式，
 *          在 `domain/op_mode/operational_mode.c` 的 `k_cmd_matrix`；
 *          两者的衔接关系见 `doc/architecture/05-安全与报警架构.md`。
 */

#ifndef DOMAIN_SAFETY_MODEL_SAFETY_MATRIX_H
#define DOMAIN_SAFETY_MODEL_SAFETY_MATRIX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/safety/model/alarm_types.h"

#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 等级行为矩阵
 *
 * | 等级     | 阻塞开洗 | 进入 LOCKOUT | 记入会话日志 |
 * |----------|----------|--------------|--------------|
 * | MINOR    | 否       | 否           | 否           |
 * | MAJOR    | 是       | 否           | 是           |
 * | CRITICAL | 是       | 是           | 是           |
 *
 * 三列的语义边界：
 *   blocks_wash_start  该等级活跃时禁止开洗（命令网关裁决 START_WASH 时读取）
 *   forces_lockout     该等级活跃时安全姿态进入 LOCKOUT（立刻停机路径）
 *   records_in_journal 触发时记入本次洗车会话日志，供洗完后评估是否转 STOPPED
 * ------------------------------------------------------------------------- */

/** @brief 单个等级的行为属性 */
typedef struct {
    bool blocks_wash_start;  /**< 活跃时禁止开洗 */
    bool forces_lockout;     /**< 活跃时安全姿态为 LOCKOUT */
    bool records_in_journal; /**< 触发时记入洗车会话日志 */
} alarm_level_behaviour_t;

/**
 * @brief  查询等级的行为属性
 * @param  level  报警等级
 * @return 行为属性；未知等级按最严格处理（全部为真），避免新增等级时默认放行
 *
 * @note   未知等级取最严格值而非最宽松：漏配置的等级若默认放行，
 *         表现为"报警存在但设备照常开洗"，比误停机危险得多。
 */
static inline alarm_level_behaviour_t alarm_level_behaviour(alarm_level_t level)
{
    switch (level) {
    case ALARM_LEVEL_MINOR:
        return (alarm_level_behaviour_t){
            .blocks_wash_start = false, .forces_lockout = false, .records_in_journal = false};
    case ALARM_LEVEL_MAJOR:
        return (alarm_level_behaviour_t){
            .blocks_wash_start = true, .forces_lockout = false, .records_in_journal = true};
    case ALARM_LEVEL_CRITICAL:
        return (alarm_level_behaviour_t){.blocks_wash_start = true, .forces_lockout = true, .records_in_journal = true};
    default:
        return (alarm_level_behaviour_t){.blocks_wash_start = true, .forces_lockout = true, .records_in_journal = true};
    }
}

/** @brief 该等级活跃时是否禁止开洗 */
static inline bool alarm_level_blocks_wash(alarm_level_t level)
{
    return alarm_level_behaviour(level).blocks_wash_start;
}

/** @brief 该等级活跃时是否要求 LOCKOUT 姿态 */
static inline bool alarm_level_forces_lockout(alarm_level_t level)
{
    return alarm_level_behaviour(level).forces_lockout;
}

/**
 * @brief  该故障形态是否在恢复起步时禁止归位
 * @note   仅安全触发（急停及项目接入的其他安全检测输入）。等级 CRITICAL 的通讯/变频器反馈不在此列。
 */
static inline bool alarm_fault_mode_blocks_recover(uint32_t fault_mode)
{
    return fault_mode == ALM_N_SAFETY_TRIGGER;
}

/** @brief 该等级触发时是否记入洗车会话日志 */
static inline bool alarm_level_records_in_journal(alarm_level_t level)
{
    return alarm_level_behaviour(level).records_in_journal;
}

/**
 * @brief  该等级是否为矩阵中已定义的取值
 * @note   与 `alarm_level_behaviour` 的 default 分支配套但方向相反：这里用于
 *         装载期拒绝未定义等级，让「取最严格值」始终是纵深防御而不是常态入口。
 *         报警目录是项目手写资产，未定义等级只会在现场表现为莫名其妙的停机。
 */
static inline bool alarm_level_is_defined(alarm_level_t level)
{
    switch (level) {
    case ALARM_LEVEL_MINOR:
    case ALARM_LEVEL_MAJOR:
    case ALARM_LEVEL_CRITICAL:
        return true;
    default:
        return false;
    }
}

/* -------------------------------------------------------------------------
 * 生命周期矩阵
 *
 * | 生命周期       | 条件消失即结案 | 允许复位结案 |
 * |----------------|----------------|--------------|
 * | FOLLOW_SIGNAL  | 是             | 否           |
 * | LATCH          | 否             | 是           |
 *
 * 运动结案不是第三档：锁存且 `reeval_group != NONE` 时，同组运动结束且
 * 条件已是 false 即删条目。复位是锁存的兜底，与有没有组无关。
 * ------------------------------------------------------------------------- */

/** @brief 单个生命周期的行为属性 */
typedef struct {
    bool clears_when_condition_gone; /**< 故障条件消失即自动结案 */
    bool allows_manual_reset;        /**< 允许复位结案（条件须已消失） */
} alarm_lifecycle_behaviour_t;

/**
 * @brief  查询生命周期的行为属性
 * @param  lifecycle  生命周期
 * @return 行为属性；未知取值按锁存处理
 *
 * @note   未知取值不自动结案：自动结案一个语义未知的报警，
 *         等于让未定义行为决定安全状态何时解除。
 */
static inline alarm_lifecycle_behaviour_t alarm_lifecycle_behaviour(alarm_lifecycle_t lifecycle)
{
    switch (lifecycle) {
    case ALARM_LIFECYCLE_FOLLOW_SIGNAL:
        return (alarm_lifecycle_behaviour_t){.clears_when_condition_gone = true, .allows_manual_reset = false};
    case ALARM_LIFECYCLE_LATCH:
        return (alarm_lifecycle_behaviour_t){.clears_when_condition_gone = false, .allows_manual_reset = true};
    default:
        return (alarm_lifecycle_behaviour_t){.clears_when_condition_gone = false, .allows_manual_reset = true};
    }
}

/** @brief 故障条件消失时是否应自动结案 */
static inline bool alarm_lifecycle_follows_signal(alarm_lifecycle_t lifecycle)
{
    return alarm_lifecycle_behaviour(lifecycle).clears_when_condition_gone;
}

/** @brief 是否为锁存（条件消失后仍留条目） */
static inline bool alarm_lifecycle_is_latch(alarm_lifecycle_t lifecycle)
{
    return !alarm_lifecycle_follows_signal(lifecycle);
}

/** @brief 是否可由复位结案 */
static inline bool alarm_lifecycle_allows_manual_reset(alarm_lifecycle_t lifecycle)
{
    return alarm_lifecycle_behaviour(lifecycle).allows_manual_reset;
}

/**
 * @brief  该生命周期是否为矩阵中已定义的取值
 * @note   用途同 `alarm_level_is_defined`：装载期拒绝，运行期仍保留最严格兜底。
 */
static inline bool alarm_lifecycle_is_defined(alarm_lifecycle_t lifecycle)
{
    switch (lifecycle) {
    case ALARM_LIFECYCLE_FOLLOW_SIGNAL:
    case ALARM_LIFECYCLE_LATCH:
        return true;
    default:
        return false;
    }
}

/**
 * @brief  由活跃报警的最高等级推导安全姿态
 * @param  highest_active_level  当前活跃报警中的最高等级
 * @param  has_any_active        是否存在活跃报警
 * @return 安全姿态
 */
static inline safety_posture_t safety_posture_from_level(alarm_level_t highest_active_level, bool has_any_active)
{
    if (!has_any_active) {
        return SAFETY_POSTURE_NOMINAL;
    }
    return alarm_level_forces_lockout(highest_active_level) ? SAFETY_POSTURE_LOCKOUT : SAFETY_POSTURE_NOMINAL;
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_MODEL_SAFETY_MATRIX_H */
