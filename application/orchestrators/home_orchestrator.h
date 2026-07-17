/**
 * @file    home_orchestrator.h
 * @brief   全机归位编排器接口（复用顺序引擎 + 归位方案）
 * @author  HUWANGWEI
 * @date    2026-07-17
 *
 * @note    start() 仅等待 engine_start 成功即返回；完成后由 worker 发布
 *          EVT_OP_MODE_HOME_COMPLETED(1/0)。engine_io 须在 init 前注册。
 */

#ifndef APPLICATION_HOME_ORCHESTRATOR_H
#define APPLICATION_HOME_ORCHESTRATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>

/**
 * @brief  初始化归位编排器（注册 home_worker 线程）
 * @pre    engine_io 与 engine_program_loader 已注册
 * @return SW_OK 成功；其它为错误码
 */
sw_err_t home_orchestrator_init(void);

/**
 * @brief  请求启动归位（调度 worker 并等待 engine_start 确认）
 * @retval SW_OK          归位流程已启动
 * @retval SW_ERR_BUSY    上一次归位尚未结束
 * @retval SW_ERR_TIMEOUT worker 启动超时
 * @retval SW_ERR_*       方案加载或启动失败
 */
sw_err_t home_orchestrator_start(void);

/**
 * @brief  中止当前归位（停止输出，worker 将以失败结束）
 */
void home_orchestrator_abort(void);

/**
 * @brief  查询归位是否正在进行
 */
bool home_orchestrator_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_HOME_ORCHESTRATOR_H */
