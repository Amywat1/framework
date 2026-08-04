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

#include <stdbool.h>

void   engine_io_sim_register(void);
void   engine_io_sim_reset(void);
void   engine_io_sim_set_signal(const char *name, int value);
void   engine_io_sim_set_axis(const char *name, double pos, double speed, bool valid);
double engine_io_sim_get_axis_pos(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_SIM_ENGINE_IO_SIM_H */
