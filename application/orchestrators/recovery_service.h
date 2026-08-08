/**
 * @file    recovery_service.h
 * @brief   Recover 用例协调（由 DEV_CMD_RECOVER 触发，订阅恢复/归位完成事件）
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
 */
sw_err_t recovery_service_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_RECOVERY_SERVICE_H */
