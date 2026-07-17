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

#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/wash/engine/engine.h"
#include "domain/wash/model/wash_types.h"

#include <stdbool.h>

/**
 * @brief  初始化洗车编排器（注册 worker_thread）
 * @pre    engine_io 后端已注册
 */
sw_err_t wash_orchestrator_init(void);

/**
 * @brief  请求启动洗车（两阶段 §5.5：调度 worker 并等待 RUNNING 确认）
 * @param  mode  洗车模式
 * @retval SW_OK          worker 已完成 engine_start 且 SESSION_STARTED 已发布
 * @retval SW_ERR_BUSY    上一次洗车尚未完成
 * @retval SW_ERR_TIMEOUT worker 启动超时
 * @retval SW_ERR_*       启动失败（模式保持 IDLE，不发布模式变更事件）
 */
sw_err_t wash_orchestrator_start(wash_mode_t mode);

/**
 * @brief  中止当前洗车（设中止原因并立即停止运动/水路）
 * @param  cause  显式中止原因（如 WASH_ABORT_MANUAL / WASH_ABORT_CRITICAL）
 */
void wash_orchestrator_abort(wash_abort_cause_t cause);

/**
 * @brief  查询洗车流程是否正在进行
 */
bool wash_orchestrator_is_busy(void);

/**
 * @brief  查询当前洗车阶段行进方向
 */
engine_direction_t wash_orchestrator_current_direction(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_WASH_ORCHESTRATOR_H */
