/**
 * @file    engine_actuator_sim.h
 * @brief   引擎执行机构仿真后端（单元测试）
 */

#ifndef ADAPTERS_OUTBOUND_HAL_SIM_ENGINE_ACTUATOR_SIM_H
#define ADAPTERS_OUTBOUND_HAL_SIM_ENGINE_ACTUATOR_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/ports/outbound/program_engine/engine_environment_port.h"

/**
 * @brief  以调用方目录绑定并返回进程级仿真执行机构实例
 * @param  catalog  执行机构目录（资源名与子通路名由调用方定义），须在实例使用期间保持有效
 * @return 成功返回实例；目录无效返回 NULL
 * @note   每次调用都会重新绑定目录，须在 engine_create 之前完成。
 */
engine_actuator_t *engine_actuator_sim_instance(const engine_actuator_catalog_t *catalog);
void               engine_actuator_sim_reset(void);

/** @brief 查询资源是否处于非 stop 状态（run/move/enable 后为真） */
int engine_actuator_sim_active(const char *resource);

/** @brief 查询资源 gear（未激活为 0） */
int engine_actuator_sim_gear(const char *resource);

/**
 * @brief  查询资源最近一次意图的 dir（未激活返回空串）
 */
const char *engine_actuator_sim_dir(const char *resource);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_SIM_ENGINE_ACTUATOR_SIM_H */
