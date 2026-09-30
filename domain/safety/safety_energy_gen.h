/**
 * @file    safety_energy_gen.h
 * @brief   切断能量代次：作废当前运动输出，不禁止后续新命令
 *
 * @note    每次实际执行 `safety_cutout_execute()` 递增一代。
 *          电机在受理 `run`/`home` 时记下代次；代次不一致则不得再写驱动，
 *          轴收到 STOPPED。不置 hold，也不闩 ESTOP / 看门狗。
 */
#ifndef DOMAIN_SAFETY_SAFETY_ENERGY_GEN_H
#define DOMAIN_SAFETY_SAFETY_ENERGY_GEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  读取当前能量代次
 * @return 单调递增的代次；上电为 0
 */
uint32_t safety_energy_gen_get(void);

/**
 * @brief  递增能量代次
 * @note   仅由 `safety_cutout_execute()` 在转入项目切断前调用；单元测试可直接调用以模拟切断。
 */
void safety_energy_gen_bump(void);

/**
 * @brief  将代次清零
 * @note   供单元测试与 `safety_output_hold_reset()` 使用，生产路径不要调用。
 */
void safety_energy_gen_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_SAFETY_ENERGY_GEN_H */
