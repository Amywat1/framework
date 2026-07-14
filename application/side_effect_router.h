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
#include "domain/command_gateway/command_types.h"
#include "domain/command_gateway/device_command.h"

/**
 * @brief  执行命令副作用
 * @param  effect  待执行副作用种类（来自 OperationalMode 裁决）
 * @param  cmd     完整入站命令
 * @retval SW_OK   副作用成功或无副作用
 */
sw_err_t side_effect_router_run(dev_cmd_effect_t effect, const dev_cmd_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_SIDE_EFFECT_ROUTER_H */
