/**
 * @file    hal_sensor_sim.h
 * @brief   传感器 HAL 仿真注入接口（供 tools/simulator/ 使用）
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef ADAPTERS_HAL_SIM_HW_SENSOR_SIM_H
#define ADAPTERS_HAL_SIM_HW_SENSOR_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/** 注册传感器仿真实现 */
void hal_sensor_sim_register(void);

/** 以下函数用于从 tools/simulator/ 注入虚拟传感器状态 */
void hal_sensor_sim_set_fwd_limit(bool v);
void hal_sensor_sim_set_rev_limit(bool v);
void hal_sensor_sim_set_lift_top(bool v);
void hal_sensor_sim_set_lift_bottom(bool v);
void hal_sensor_sim_set_estop(bool v);
void hal_sensor_sim_encoder_tick(int delta);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_SIM_HW_SENSOR_SIM_H */
