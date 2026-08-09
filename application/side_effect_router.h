/**
 * @file    side_effect_router.h
 * @brief   命令副作用路由（application 层，不做权限判断）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef APPLICATION_SIDE_EFFECT_ROUTER_H
#define APPLICATION_SIDE_EFFECT_ROUTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"

/**
 * @brief  按命令种类执行同步副作用
 * @param  cmd          完整入站命令（已裁决 ALLOWED）
 * @param  mode_before  裁决前的运行模式（STOP_ALL 据此决定是否 abort 会话）
 * @retval SW_OK        成功或该命令无同步副作用
 * @note   STOP/RESUME/RECOVER 为空操作；RECOVER 的异步编排由 domain 进态事件触发。
 */
sw_err_t side_effect_router_run(const dev_cmd_t *cmd, operational_mode_t mode_before);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_SIDE_EFFECT_ROUTER_H */
