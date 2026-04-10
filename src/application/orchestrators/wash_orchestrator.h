/**
 * @file    wash_orchestrator.h
 * @brief   洗车流程编排器接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    wash_worker_thread 由 core/scheduler 统一创建，
 *          orchestrator 持有信号量用于唤醒线程。
 *          内部调用 domain/process/step_engine 同步执行每一步，
 *          完成后发布 EVT_WASH_DONE / EVT_WASH_ABORTED。
 */

#ifndef APPLICATION_WASH_ORCHESTRATOR_H
#define APPLICATION_WASH_ORCHESTRATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/wash_types.h"
#include "common/sw_error.h"
#include <stdbool.h>

/**
 * @brief  初始化洗车编排器（注册 worker_thread、订阅 LOCKOUT 事件）
 */
sw_err_t wash_orchestrator_init(void);

/**
 * @brief  启动洗车流程（唤醒 worker_thread）
 * @retval SW_OK       启动成功
 * @retval SW_ERR_BUSY 上一次洗车未结束
 * @retval SW_ERR_STATE 有 ERROR 报警激活
 */
sw_err_t wash_orchestrator_start(wash_mode_t mode);

/**
 * @brief  中止当前洗车（设置中止标志，worker_thread 下一步检测后退出）
 */
void wash_orchestrator_abort(void);

/**
 * @brief  查询洗车流程是否正在进行
 */
bool wash_orchestrator_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_WASH_ORCHESTRATOR_H */
