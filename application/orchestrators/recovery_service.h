/**
 * @file    recovery_service.h
 * @brief   Recover 用例协调实现（复位锁存告警 + 异步全归位 + 完成后验证）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef APPLICATION_ORCHESTRATORS_RECOVERY_SERVICE_H
#define APPLICATION_ORCHESTRATORS_RECOVERY_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化恢复协调器并订阅相关事件
 * @retval SW_OK        订阅成功
 * @retval SW_ERR_*     event_subscribe_table 失败
 *
 * @note   离开 RECOVERING 时自行取消归位等待（订阅 EVT_OP_MODE_CHANGED）；
 *         迟到的 HOME_COMPLETED 在模式已非 RECOVERING 时忽略。
 *         归位成功后 reset_all；ON_MOTION 姿态证明由项目在发布 HOME_COMPLETED 前完成。
 */
sw_err_t recovery_service_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_RECOVERY_SERVICE_H */
