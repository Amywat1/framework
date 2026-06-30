/**
 * @file    command_guard.h
 * @brief   外部命令同步准入校验（command_port 注入前调用）
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    读取 dev_ctx 判断当前是否允许下达命令；不负责事件投递与状态编排。
 */

#ifndef APPLICATION_COMMAND_GUARD_H
#define APPLICATION_COMMAND_GUARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ports/cloud/command_port.h"
#include "common/sw_error.h"

/**
 * @brief  校验命令在当前设备/安全态下是否允许注入
 * @param  cmd  待注入命令（不可为 NULL）
 * @retval SW_OK         允许注入
 * @retval SW_ERR_STATE  当前状态不允许
 * @retval SW_ERR_PARAM  参数或命令类型无效
 */
sw_err_t command_guard_check(const cmd_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_COMMAND_GUARD_H */
