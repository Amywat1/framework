/**
 * @file    safety_supervisor.h
 * @brief   安全监督者接口（safety_fsm 事件 → dev_ctx 更新）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    订阅 EVT_SAFETY_* 和 EVT_ALARM_* 事件，将安全域状态同步到 dev_ctx。
 *          不直接控制执行机构，只做状态聚合。
 */

#ifndef APPLICATION_SAFETY_SUPERVISOR_H
#define APPLICATION_SAFETY_SUPERVISOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化安全监督者（订阅安全/报警事件）
 */
sw_err_t safety_supervisor_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_SAFETY_SUPERVISOR_H */
