/**
 * @file    wash_orchestrator.h
 * @brief   洗车流程编排器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    wash_worker_thread 由 scheduler 创建；引擎以固定周期 tick 驱动
 *          方案 JSON 描述的洗车流程，完成后发布 EVT_WASH_DONE / EVT_WASH_ABORTED。
 *          engine_io 后端须在 wash_orchestrator_init() 前由调用方注册
 *          （M8 真机：engine_io_m8_register()，仿真：engine_io_sim_register()）。
 */

#ifndef APPLICATION_WASH_ORCHESTRATOR_H
#define APPLICATION_WASH_ORCHESTRATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/wash/model/wash_types.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>

/**
 * @brief  初始化洗车编排器（注册 worker_thread）
 * @pre    engine_io 后端已注册
 */
sw_err_t wash_orchestrator_init(void);

/**
 * @brief  启动洗车流程（唤醒 worker_thread）
 * @param  mode  洗车模式
 * @retval SW_OK       启动成功
 * @retval SW_ERR_BUSY 上一次洗车尚未完成
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

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_WASH_ORCHESTRATOR_H */
