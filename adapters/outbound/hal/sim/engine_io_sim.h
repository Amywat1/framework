/**
 * @file    engine_io_sim.h
 * @brief   引擎 IO 后端的仿真内存实现（按名字符串键存取 DI/DO/坐标轴）
 * @author  huwangwei
 * @date    2026-06-25
 *
 * @note    本后端不含任何物理因果，仅做"存与取"：DI/DO 为整数值，坐标轴为
 *          位置/速度/有效性。测试或设备物理模型通过注入/读取接口驱动它，
 *          引擎则通过已注册的 engine_io_ops_t 读写它。仅用于 sim 构建与测试。
 */

#ifndef ADAPTERS_HAL_SIM_HW_ENGINE_IO_SIM_H
#define ADAPTERS_HAL_SIM_HW_ENGINE_IO_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief  将仿真后端注册到引擎 IO 接口（engine_io_register）
 */
void engine_io_sim_register(void);

/**
 * @brief  清空所有仿真 DI/DO/坐标轴状态
 */
void engine_io_sim_reset(void);

/**
 * @brief  注入 DI 信号逻辑值
 * @param  name   DI 枚举名
 * @param  value  逻辑值（0/1 或整数）
 */
void engine_io_sim_set_signal(const char *name, int value);

/**
 * @brief  读取 DO 输出当前值（未写过返回 0）——测试断言引擎输出用
 */
int engine_io_sim_get_output(const char *name);

/**
 * @brief  设置坐标轴位置/速度/有效性
 * @param  name   坐标轴 ID
 * @param  pos    位置（工程单位）
 * @param  speed  速度
 * @param  valid  是否有效
 */
void engine_io_sim_set_axis(const char *name, double pos, double speed, bool valid);

/**
 * @brief  读取坐标轴位置（未设置返回 0）
 */
double engine_io_sim_get_axis_pos(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_SIM_HW_ENGINE_IO_SIM_H */
