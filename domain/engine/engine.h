/**
 * @file    engine.h
 * @brief   通用控制引擎运行时（tick 驱动的方案执行器）
 * @author  huwangwei
 * @date    2026-06-25
 *
 * @note    引擎加载方案后，由外部以固定周期调用 engine_tick(dt_ms) 推进：
 *          联锁监控（最高优先级）→ 标记锁存 → 阶段串行 → 通道并行步骤。
 *          所有 IO 经 engine_io 后端按名读写，引擎不感知具体硬件。
 */

#ifndef DOMAIN_ENGINE_ENGINE_H
#define DOMAIN_ENGINE_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "common/sw_error.h"
#include "domain/engine/engine_model.h"

/** 引擎运行状态 */
typedef enum
{
    ENGINE_STATE_IDLE = 0,     /* 未启动 */
    ENGINE_STATE_RUNNING,      /* 正在执行阶段 */
    ENGINE_STATE_PHASE_HALTED, /* halt_phase：等待恢复 */
    ENGINE_STATE_HALTED,       /* halt_all：程序终止 */
    ENGINE_STATE_DONE          /* 全部阶段完成 */
} engine_run_state_t;

/** 引擎实例（不透明） */
typedef struct engine engine_t;

/**
 * @brief  创建引擎实例
 * @return 实例指针；失败返回 NULL
 */
engine_t *engine_create(void);

/**
 * @brief  销毁引擎实例（同时释放其持有的方案）
 */
void engine_destroy(engine_t *e);

/**
 * @brief  装载方案（引擎接管其所有权，destroy 时释放）
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t engine_load_program(engine_t *e, engine_program_t *prog);

/**
 * @brief  启动执行（从第一个阶段开始）；需先装载方案
 * @retval SW_OK / SW_ERR_STATE
 */
sw_err_t engine_start(engine_t *e);

/**
 * @brief  推进一个时间片
 * @param  dt_ms  本次时间片长度（毫秒）
 */
void engine_tick(engine_t *e, uint32_t dt_ms);

/**
 * @brief  从 halt_phase 恢复，重新进入当前阶段
 * @retval SW_OK / SW_ERR_STATE
 */
sw_err_t engine_recover(engine_t *e);

/** @brief  查询运行状态 */
engine_run_state_t engine_state(const engine_t *e);

/** @brief  当前阶段下标（未运行返回 -1） */
int engine_current_phase(const engine_t *e);

/** @brief  当前阶段 id（未运行返回 NULL） */
const char *engine_current_phase_id(const engine_t *e);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_ENGINE_ENGINE_H */
