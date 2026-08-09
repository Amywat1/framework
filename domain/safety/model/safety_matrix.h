/**
 * @file    safety_matrix.h
 * @brief   安全行为矩阵：报警等级与清除策略的语义收口
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    解决的问题：等级与清除策略的语义原先散落在判定点上——
 *          `level >= ALARM_LEVEL_MAJOR` 出现在 3 处、`level == CRITICAL` 出现在
 *          2 处、清除策略判定出现在 3 处。每处都在重新表达"MAJOR 意味着什么"，
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

/** @brief 该等级触发时是否记入洗车会话日志 */
static inline bool alarm_level_records_in_journal(alarm_level_t level)
{
    return alarm_level_behaviour(level).records_in_journal;
}

/* -------------------------------------------------------------------------
 * 清除策略矩阵
 *
 * | 策略         | 条件消失即清除 | 需运动重评估 | 需手动复位 |
 * |--------------|----------------|--------------|------------|
 * | AUTO_STATIC  | 是             | 否           | 否         |
 * | ON_MOTION    | 否             | 是           | 是（兜底） |
 * | MANUAL_RESET | 否             | 否           | 是         |
 *
 * ON_MOTION 两列同时为真：正常路径由运动完成触发重评估自动清除；
 * 若相关运动一直未发生，仍允许手动复位兜底，避免报警永久卡住。
 * ------------------------------------------------------------------------- */

/** @brief 单个清除策略的行为属性 */
typedef struct {
    bool clears_when_condition_gone; /**< 故障条件消失即自动清除 */
    bool needs_motion_reeval;        /**< 由运动完成触发重评估清除 */
    bool allows_manual_reset;        /**< 允许手动复位清除 */
} alarm_clear_behaviour_t;

/**
 * @brief  查询清除策略的行为属性
 * @param  clear  清除策略
 * @return 行为属性；未知策略按"仅允许手动复位"处理
 *
 * @note   未知策略不自动清除：自动清除一个语义未知的报警，
 *         等于让未定义行为决定安全状态何时解除。
 */
static inline alarm_clear_behaviour_t alarm_clear_behaviour(alarm_clear_t clear)
{
    switch (clear) {
    case ALARM_CLEAR_AUTO_STATIC:
        return (alarm_clear_behaviour_t){
            .clears_when_condition_gone = true, .needs_motion_reeval = false, .allows_manual_reset = false};
    case ALARM_CLEAR_ON_MOTION:
        return (alarm_clear_behaviour_t){
            .clears_when_condition_gone = false, .needs_motion_reeval = true, .allows_manual_reset = true};
    case ALARM_CLEAR_MANUAL_RESET:
        return (alarm_clear_behaviour_t){
            .clears_when_condition_gone = false, .needs_motion_reeval = false, .allows_manual_reset = true};
    default:
        return (alarm_clear_behaviour_t){
            .clears_when_condition_gone = false, .needs_motion_reeval = false, .allows_manual_reset = true};
    }
}

/** @brief 故障条件消失时是否应自动清除 */
static inline bool alarm_clear_is_auto(alarm_clear_t clear)
{
    return alarm_clear_behaviour(clear).clears_when_condition_gone;
}

/** @brief 是否参与运动完成后的重评估 */
static inline bool alarm_clear_needs_motion_reeval(alarm_clear_t clear)
{
    return alarm_clear_behaviour(clear).needs_motion_reeval;
}

/** @brief 是否可由手动复位清除 */
static inline bool alarm_clear_allows_manual_reset(alarm_clear_t clear)
{
    return alarm_clear_behaviour(clear).allows_manual_reset;
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
