/**
 * @file    step_engine.h
 * @brief   洗车单步同步执行器接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    step_engine 是纯同步执行器，在 wash_worker_thread 中被调用。
 *          每次调用 step_engine_exec_step() 执行一个步骤：
 *            1. apply_step：启动龙门/刷子/水路/升降等执行机构
 *            2. wait_exit：循环轮询退出条件（限位 / 脉冲位置 / 超时）
 *          函数返回后，执行机构状态由调用方（wash_orchestrator）决定是否停止。
 *
 *          中止通道：step_engine_abort() 设置原子标志，wait_exit 每轮检测。
 */

#ifndef DOMAIN_PROCESS_STEP_ENGINE_H
#define DOMAIN_PROCESS_STEP_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/process/recipe.h"
#include "common/sw_error.h"
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化步骤执行器（清零中止标志）
 */
sw_err_t step_engine_init(void);

/**
 * @brief  同步执行一个洗车步骤
 *         阻塞直到退出条件满足、超时或收到中止信号。
 * @param  step        步骤配置（来自 recipe_get）
 * @param  timeout_ms  单步最长超时（ms，建议 120000）
 * @param  brush_freq  刷子频率（0.01Hz，由调用方从参数表获取后传入）
 * @retval SW_OK           步骤正常完成
 * @retval SW_ERR_TIMEOUT  步骤超时
 * @retval SW_ERR_STATE    报警或中止信号触发
 */
sw_err_t step_engine_exec_step(const wash_step_config_t *step,
                                uint32_t timeout_ms,
                                uint16_t brush_freq);

/**
 * @brief  请求中止当前步骤（线程安全，可从任意线程调用）
 */
void step_engine_abort(void);

/**
 * @brief  清除中止标志（新一轮洗车开始前调用）
 */
void step_engine_clear_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PROCESS_STEP_ENGINE_H */
