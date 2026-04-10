/**
 * @file    device_fsm.h
 * @brief   设备状态机接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    状态切换由事件驱动完成，
 *          不依赖独立轮询线程。
 */

#ifndef APPLICATION_DEVICE_FSM_H
#define APPLICATION_DEVICE_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/device_state.h"
#include "common/sw_error.h"

/**
 * @brief  初始化设备 FSM（订阅命令/安全/流程事件，设置初始状态 IDLE）
 */
sw_err_t device_fsm_init(void);

/**
 * @brief  获取当前设备状态
 */
dev_state_t device_fsm_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_DEVICE_FSM_H */
