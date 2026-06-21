/**
 * @file    device_fsm.h
 * @brief   设备状态机接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    状态切换由事件驱动完成；设备态存储于 dev_ctx，本模块不维护镜像。
 */

#ifndef APPLICATION_DEVICE_FSM_H
#define APPLICATION_DEVICE_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化设备 FSM（订阅命令/安全/流程事件，设置初始状态 IDLE）
 */
sw_err_t device_fsm_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_DEVICE_FSM_H */
