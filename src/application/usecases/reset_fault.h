/**
 * @file    reset_fault.h
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef APPLICATION_USECASE_RESET_FAULT_H
#define APPLICATION_USECASE_RESET_FAULT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  故障复位（检查状态为 FAULT 后发布 EVT_CMD_RESET_FAULT）
 * @retval SW_ERR_STATE 设备非 FAULT 状态
 */
sw_err_t reset_fault(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_USECASE_RESET_FAULT_H */
