/**
 * @file    safety_types.h
 * @brief   安全域相关类型定义
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef DOMAIN_SAFETY_TYPES_H
#define DOMAIN_SAFETY_TYPES_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * 安全状态机状态
 * ------------------------------------------------------------------------- */
typedef enum
{
    SAFETY_STATE_OK       = 0, /* 正常运行 */
    SAFETY_STATE_WARNING,      /* 降级运行（非关键故障，当前动作可继续，完成后停机）*/
    SAFETY_STATE_LOCKOUT,      /* 全部停机，所有运动禁止，等待复位 */
} safety_state_t;

/* -------------------------------------------------------------------------
 * 报警等级
 * ------------------------------------------------------------------------- */
typedef enum
{
    ALARM_LEVEL_ERROR   = 0, /* 立即停机（→ LOCKOUT）*/
    ALARM_LEVEL_WARNING = 1, /* 当前工作完成后停机（→ WARNING）*/
    ALARM_LEVEL_NOTICE  = 2, /* 仅上报，不影响运行 */
} alarm_level_t;

/* -------------------------------------------------------------------------
 * 报警恢复方式（可按位组合）
 * ------------------------------------------------------------------------- */
#define ALARM_RECOVER_AUTO    (1U << 0) /* 触发条件消失后自动恢复 */
#define ALARM_RECOVER_DRIVE   (1U << 1) /* 驱动复位后恢复（VFD 故障复位脉冲）*/
#define ALARM_RECOVER_MANUAL  (1U << 2) /* 仅允许人工按复位按钮清除 */

#endif /* DOMAIN_SAFETY_TYPES_H */
