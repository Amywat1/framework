/**
 * @file    engine_io_sim.h
 * @brief   引擎 IO 仿真后端（按名存取 DI / 坐标轴）
 * @author  huwangwei
 * @date    2026-06-25
 */

#ifndef ADAPTERS_OUTBOUND_HAL_SIM_ENGINE_IO_SIM_H
#define ADAPTERS_OUTBOUND_HAL_SIM_ENGINE_IO_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/ports/outbound/program_engine/engine_environment_port.h"

#include <stdbool.h>

/**
 * @brief  以调用方目录绑定并返回进程级仿真 IO 实例
 * @param  catalog  IO 目录（信号名与轴名由调用方定义），须在实例使用期间保持有效
 * @return 成功返回实例；目录无效返回 NULL
 * @note   每次调用都会重新绑定目录，须在 engine_create 之前完成。
 */
engine_io_t *engine_io_sim_instance(const engine_io_catalog_t *catalog);
void         engine_io_sim_reset(void);
void         engine_io_sim_set_signal(const char *name, int value);
void         engine_io_sim_set_axis(const char *name, double pos, double speed, bool valid);
double       engine_io_sim_get_axis_pos(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_SIM_ENGINE_IO_SIM_H */
