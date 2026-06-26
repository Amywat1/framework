/**
 * @file    safety_types.h
 * @brief   安全域状态类型定义
 * @author  HUWANGWEI
 * @date    2026-06-26
 *
 * @note    safety_state_t 是「整机安全姿态」的聚合结果，由 domain/safety/safety_fsm
 *          根据当前活跃报警的最高等级派生，供执行机构/设备 FSM 解耦消费。
 *          它与单条报警的 alarm_level_t 是两条不同的轴：等级描述「某条故障多严重」，
 *          安全态描述「当前所有故障合起来机器能否运行」。
 */

#ifndef DOMAIN_MODEL_SAFETY_TYPES_H
#define DOMAIN_MODEL_SAFETY_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 安全态（数值越大越严重）
 * ------------------------------------------------------------------------- */
typedef enum
{
    SAFETY_STATE_OK = 0,    /**< 无影响运行的活跃报警 */
    SAFETY_STATE_WARNING,   /**< 存在 MAJOR 报警，降级运行（本期不驱动动作）*/
    SAFETY_STATE_LOCKOUT,   /**< 存在 CRITICAL 报警，必须停机 */
} safety_state_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_MODEL_SAFETY_TYPES_H */
