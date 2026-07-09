/**
 * @file    safety_types.h
 * @brief   安全域姿态类型定义
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef DOMAIN_MODEL_SAFETY_TYPES_H
#define DOMAIN_MODEL_SAFETY_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SAFETY_POSTURE_NOMINAL = 0,
    SAFETY_POSTURE_LOCKOUT,
} safety_posture_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_MODEL_SAFETY_TYPES_H */
