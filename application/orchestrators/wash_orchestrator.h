/**
 * @file    wash_orchestrator.h
 * @brief   洗车流程编排器接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    wash_worker_thread 由 scheduler 创建；单步同步执行与轮询等待均在本模块内完成，
 *          完成后发布 EVT_WASH_DONE / EVT_WASH_ABORTED。
 */

#ifndef APPLICATION_WASH_ORCHESTRATOR_H
#define APPLICATION_WASH_ORCHESTRATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/wash_types.h"
#include "domain/process/recipe.h"
#include "common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  初始化洗车编排器（注册 worker_thread）
 */
sw_err_t wash_orchestrator_init(void);

/**
 * @brief  启动洗车流程（唤醒 worker_thread）
 */
sw_err_t wash_orchestrator_start(wash_mode_t mode);

/**
 * @brief  中止当前洗车（设中止标志并立即停止运动/水路）
 */
void wash_orchestrator_abort(void);

/**
 * @brief  查询洗车流程是否正在进行
 */
bool wash_orchestrator_is_busy(void);

/**
 * @brief  同步执行一个洗车步骤（wash_worker 与单元测试使用）
 */
sw_err_t wash_exec_step(const wash_step_config_t *step,
                        uint32_t timeout_ms,
                        uint16_t brush_freq);

/**
 * @brief  清除步骤中止标志（新一轮洗车开始前调用）
 */
void wash_exec_clear_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_WASH_ORCHESTRATOR_H */
