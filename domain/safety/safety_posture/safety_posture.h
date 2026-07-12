/**
 * @file    safety_posture.h
 * @brief   安全姿态两态聚合（NOMINAL / LOCKOUT）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef DOMAIN_SAFETY_SAFETY_POSTURE_H
#define DOMAIN_SAFETY_SAFETY_POSTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化安全姿态聚合并订阅 EVT_ALARM_*
 * @retval SW_OK 订阅成功
 */
sw_err_t safety_posture_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_SAFETY_POSTURE_H */
