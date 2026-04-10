/**
 * @file    device_fsm.h
 * @brief   设备顶层有限状态机接口（事件驱动）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    状态转移完全由事件驱动，在 event_dispatch_thread 上下文执行。
 *          不使用独立轮询线程（与原 app_fsm.c 的 100ms 循环不同）。
 *
 *          状态机：
 *            INIT  → IDLE（init 完成后直接置位）
 *            IDLE  → RUN    （EVT_CMD_ORDER）
 *            IDLE  → STOP   （EVT_CMD_STOP_OPERATION）
 *            RUN   → IDLE   （EVT_WASH_DONE，经 COMPLETE 动作）
 *            RUN   → FAULT  （EVT_WASH_ABORTED / EVT_SAFETY_LOCKOUT）
 *            FAULT → IDLE   （EVT_CMD_RESET_FAULT，报警清除后）
 *            STOP  → IDLE   （EVT_CMD_RESUME_OPERATION）
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
