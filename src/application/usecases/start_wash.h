/**
 * @file    start_wash.h
 * @brief   启动洗车用例接口
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef APPLICATION_USECASE_START_WASH_H
#define APPLICATION_USECASE_START_WASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/wash_types.h"
#include "common/sw_error.h"

/**
 * @brief  检查前置条件并发布 EVT_CMD_ORDER
 * @retval SW_OK         命令已投递
 * @retval SW_ERR_STATE  设备非 IDLE 或安全状态非 OK
 */
sw_err_t start_wash(wash_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_USECASE_START_WASH_H */
