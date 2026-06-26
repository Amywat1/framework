/**
 * @file    m8_device_model.h
 * @brief   M8 洗车机设备物理仿真模型（场景测试支撑，非产品代码）
 * @author  huwangwei
 * @date    2026-06-25
 *
 * @note    读取引擎写出的 DO（经 engine_io_sim），按物理因果更新 DI 信号与
 *          龙门坐标轴，并生成 RADAR_CAR_TAIL 等"合成信号"，使整套洗车方案
 *          能在仿真下端到端跑通。仅供 tests/ 使用，不参与真机/产品构建。
 */

#ifndef TESTS_SUPPORT_M8_DEVICE_MODEL_H
#define TESTS_SUPPORT_M8_DEVICE_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  复位模型并预置初始信号（龙门后限位、升降下限位、锁止未归位）
 */
void m8_device_model_init(void);

/**
 * @brief  推进一拍物理仿真（读 DO → 更新 DI/轴/合成信号）
 * @param  dt_ms  时间片（当前模型使用固定步长，dt 仅作语义占位）
 */
void m8_device_model_tick(uint32_t dt_ms);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_SUPPORT_M8_DEVICE_MODEL_H */
